#include <wfc.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include "sgIP/sgIP.h"

//#define WFC_DEBUG

#if !defined(WFC_DEBUG)
#define dietPrint(...) ((void)0)
#endif

typedef enum WfcProc {
	WfcProc_None    = 0,
	WfcProc_Scan    = 1,
	WfcProc_Connect = 2,
} WfcProc;

typedef enum WfcPhase {
	WfcPhase_None    = 0,
	WfcPhase_Scan    = 1,
	WfcPhase_Connect = 2,
	WfcPhase_SetupIP = 3,
} WfcPhase;

alignas(32) static WfcConnSlotEx s_wfcSlots[WFC_MAX_CONN_SLOTS];
alignas(32) static WlanBssDesc s_wfcBssDesc[WLAN_MAX_BSS_ENTRIES];

static Thread s_wfcIPThread;
alignas(8) static u8 s_wfcIPThreadStack[2048];

static struct {
	sgIP_Hub_HWInterface* iface;
	u8 cur_slot;
	u8 num_slots;
	u8 cur_bss;
	u8 num_bss;
	WfcProc cur_process;
	bool dhcp_active;

	union {
		struct {
			WlanBssScanFilter const* filter;
		} scan;

		struct {
			WlanBssDesc const* bss;
			WlanAuthData const* auth;
		} connect;
	};

	Mailbox ip_mbox;
} s_wfcState;

// TODO: Replace with custom heap

void* sgIP_malloc(int size)
{
	size = (size+1)&~1;
	return malloc(size);
}

void sgIP_free(void* ptr)
{
	free(ptr);
}

void sgIP_IntrWaitEvent(void)
{
	// TODO: use some kind of condvar?
	threadIrqWait(false, IRQ_PXI_RECV | IRQ_TIMER3);
}

static void _wfcIPTimerTask(TickTask* t)
{
	Mailbox* mb = &s_wfcState.ip_mbox;
	if (mb->pending_slots == 0) {
		mailboxTrySend(mb, 1);
	}
}

static int _wfcIPThreadMain(void* arg)
{
	u32 slots[2];
	mailboxPrepare(&s_wfcState.ip_mbox, slots, 2);

	TickTask task;
	unsigned ticks = ticksFromUsec(50000); // 50ms
	tickTaskStart(&task, _wfcIPTimerTask, ticks, ticks);

	dietPrint("[IPThr] Start\n");

	for (;;) {
		u32 msg = mailboxRecv(&s_wfcState.ip_mbox);
		if (!msg) {
			tickTaskStop(&task);
			break;
		}

		SGIP_INTR_PROTECT();

		// Update sgIP periodic processes
		sgIP_Timer(50);

		// Update DHCP if needed
		if (s_wfcState.dhcp_active) {
			switch (sgIP_DHCP_Update()) {
				case SGIP_DHCP_STATUS_WORKING: break;

				case SGIP_DHCP_STATUS_SUCCESS: {
					dietPrint("[IPThr] DHCP ok, connected!\n");
					sgIP_ARP_SendGratARP(s_wfcState.iface);
					s_wfcState.dhcp_active = false;
					s_wfcState.cur_process = WfcProc_None;
					break;
				}

				default:
				case SGIP_DHCP_STATUS_IDLE:
				case SGIP_DHCP_STATUS_FAILED: {
					dietPrint("[IPThr] DHCP fail, giving up\n");
					wlmgrDisassociate();
					break;
				}
			}
		}

		SGIP_INTR_UNPROTECT();
	}

	dietPrint("[IPThr] Stop\n");

	return 0;
}

static int _wfcSend(sgIP_Hub_HWInterface* hw, sgIP_memblock* mb)
{
	if (mb->next || mb->thislength != mb->totallength) {
		// Shouldn't happen, but just to be sure
		dietPrint("[WFC] Fragmented sgIP memblock\n");
		sgIP_memblock_free(mb);
		return 1;
	}

	NetBuf* pPacket;
	unsigned headroom = g_envExtraInfo->wlmgr_hdr_headroom_sz;
	if (!(pPacket = netbufAlloc(headroom, mb->thislength, NetBufPool_Tx))) {
		// Drop the packet if we cannot allocate a TX netbuf.
		// This is done in order to break a deadlock between the ARM9 and ARM7
		// where the 9 is waiting for the 7 to release TX mem, but the 7 is
		// waiting for the 9 to release RX mem. In other words, we prioritize
		// receiving packets to sending packets.
		dietPrint("[WFC] Out of netbuf TX mem\n");
		sgIP_memblock_free(mb);
		return 1;
	}

	memcpy(netbufGet(pPacket), mb->datastart, mb->thislength);
	sgIP_memblock_free(mb);
	wlmgrRawTx(pPacket);

	return 0;
}

static void _wfcStartIP(void)
{
	sgIP_Hub_HWInterface* iface = s_wfcState.iface;
	if (iface->flags & SGIP_FLAG_HWINTERFACE_ENABLED) {
		// No need to do anything
		return;
	}

	// Initialize MAC address
	iface->hwaddrlen = 6;
	memcpy(iface->hwaddr, g_envExtraInfo->wlmgr_macaddr, 6);
	dietPrint("[IP] MAC = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n",
		iface->hwaddr[0], iface->hwaddr[1], iface->hwaddr[2], iface->hwaddr[3], iface->hwaddr[4], iface->hwaddr[5]);

	// Copy WFC config
	WfcConnSlot* slot = s_wfcState.cur_slot < WFC_MAX_CONN_SLOTS ? &s_wfcSlots[s_wfcState.cur_slot].base : NULL;
	if (slot && slot->ipv4_addr && slot->ipv4_gateway && slot->ipv4_subnet) {
		iface->ipaddr = slot->ipv4_addr;
		iface->gateway = slot->ipv4_gateway;
		iface->snmask = htonl(~((1U << (32 - slot->ipv4_subnet)) - 1));
		s_wfcState.dhcp_active = false;
	} else {
		iface->ipaddr = 0;
		iface->gateway = htonl((169<<24) | (254<<16) | 1);
		iface->snmask = htonl((255<<24) | (255<16));
		s_wfcState.dhcp_active = true;
	}
	iface->dns[0] = slot ? slot->ipv4_dns[0] : 0;
	iface->dns[1] = slot ? slot->ipv4_dns[1] : 0;
	iface->flags |= SGIP_FLAG_HWINTERFACE_ENABLED;

	// Start sgIP thread
	threadPrepare(&s_wfcIPThread, _wfcIPThreadMain, NULL, &s_wfcIPThreadStack[sizeof(s_wfcIPThreadStack)], threadGetSelf()->baseprio);
	threadAttachLocalStorage(&s_wfcIPThread, NULL);
	threadStart(&s_wfcIPThread);

	SGIP_INTR_PROTECT();
	if (s_wfcState.dhcp_active) {
		// Start DHCP!
		sgIP_DHCP_Start(iface, iface->dns[0] == 0);
	} else {
		// Send gratuituous ARP now
		sgIP_ARP_SendGratARP(iface);
	}
	SGIP_INTR_UNPROTECT();
}

static void _wfcStopIP(void)
{
	sgIP_Hub_HWInterface* iface = s_wfcState.iface;
	if (!(iface->flags & SGIP_FLAG_HWINTERFACE_ENABLED)) {
		// No need to do anything
		return;
	}

	// Bring down sgIP hardware interface
	iface->flags &= ~SGIP_FLAG_HWINTERFACE_ENABLED;
	iface->ipaddr = 0;
	iface->gateway = 0;
	iface->snmask = 0;
	iface->dns[0] = 0;
	iface->dns[1] = 0;

	// Stop sgIP thread
	mailboxTrySend(&s_wfcState.ip_mbox, 0);
	threadJoin(&s_wfcIPThread);
}

static void _wfcOnEvent(void* user, WlMgrEvent event, uptr arg0, uptr arg1)
{
	WfcPhase phase = WfcPhase_None;

	switch (event) {
		default: {
			dietPrint("[WFC] unkevent %u %u %u\n", event, arg0, arg1);
			break;
		}

		case WlMgrEvent_NewState: {
			WlMgrState old_state = (WlMgrState)arg1;
			WlMgrState new_state = (WlMgrState)arg0;

			if (old_state == WlMgrState_Associated) {
				dietPrint("[WFC] kicked out\n");
				_wfcStopIP();
			}

			if (old_state >= WlMgrState_Idle && new_state <= WlMgrState_Stopping) {
				dietPrint("[WFC] stopping\n");
				s_wfcState.cur_process = WfcProc_None;
			}

			switch (s_wfcState.cur_process) {
				default: break;

				case WfcProc_Scan: {
					if (new_state == WlMgrState_Idle) {
						if (old_state <= WlMgrState_Idle) {
							phase = WfcPhase_Scan;
						} else {
							s_wfcState.cur_process = WfcProc_None;
						}
					}

					break;
				}

				case WfcProc_Connect: {
					bool next_slot = false;
					if (new_state == WlMgrState_Idle) {
						if (s_wfcState.cur_slot >= WFC_MAX_CONN_SLOTS) {
							if (old_state <= WlMgrState_Idle) {
								phase = WfcPhase_Connect;
							} else {
								s_wfcState.cur_process = WfcProc_None;
							}
						} else if (old_state <= WlMgrState_Idle) {
							phase = WfcPhase_Scan;
						} else if (old_state == WlMgrState_Scanning) {
							if (s_wfcState.num_bss) {
								phase = WfcPhase_Connect;
							} else {
								next_slot = true;
							}
						} else if (old_state == WlMgrState_Associating) {
							s_wfcState.cur_bss ++;
							if (s_wfcState.cur_bss < s_wfcState.num_bss) {
								phase = WfcPhase_Connect;
							} else {
								next_slot = true;
							}
						} else if (old_state >= WlMgrState_Associated) {
							next_slot = true;
						}
					} else if (new_state == WlMgrState_Associated) {
						phase = WfcPhase_SetupIP;
					}

					if (next_slot) {
						s_wfcState.cur_slot ++;
						if (s_wfcState.cur_slot < s_wfcState.num_slots) {
							dietPrint("[WFC] trying next slot\n");
							phase = WfcPhase_Scan;
						} else {
							dietPrint("[WFC] no more slots - FAIL\n");
							s_wfcState.cur_process = WfcProc_None;
						}
					}

					break;
				}
			}

			break;
		}

		case WlMgrEvent_CmdFailed: {
			dietPrint("[WFC] cmd%u fail\n", arg0);
			s_wfcState.cur_process = WfcProc_None;
			break;
		}

		case WlMgrEvent_ScanComplete: {
			s_wfcState.cur_bss = 0;
			s_wfcState.num_bss = arg1;
			dietPrint("[WFC] found %u\n", arg1);
			break;
		}

		case WlMgrEvent_Disconnected: {
			dietPrint("[WFC] disassoc reason %u\n", arg0);
			break;
		}
	}

	switch (phase) {
		default:
		case WfcPhase_None: break;

		case WfcPhase_Scan: {
			if (s_wfcState.cur_process == WfcProc_Scan) {
				wlmgrStartScan(s_wfcBssDesc, s_wfcState.scan.filter);
				break;
			}

			// Retrieve slot
			WfcConnSlotEx* slot = &s_wfcSlots[s_wfcState.cur_slot];
			unsigned ssidlen = strnlen(slot->base.ssid, WLAN_MAX_SSID_LEN);
			dietPrint("[WFC] searching %.*s\n", ssidlen, slot->base.ssid);

			// Attempt to find BSSs for the current slot
			WlanBssScanFilter filter = {
				.channel_mask = UINT32_MAX,
				.target_bssid = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
			};

			filter.target_ssid_len = ssidlen;
			memcpy(filter.target_ssid, slot->base.ssid, ssidlen);
			wlmgrStartScan(s_wfcBssDesc, &filter);
			break;
		}

		case WfcPhase_Connect: {
			if (s_wfcState.cur_slot >= WFC_MAX_CONN_SLOTS) {
				wlmgrAssociate(s_wfcState.connect.bss, s_wfcState.connect.auth);
				break;
			}

			WfcConnSlotEx* slot = &s_wfcSlots[s_wfcState.cur_slot];
			WlanBssDesc* bss = &s_wfcBssDesc[s_wfcState.cur_bss];

			dietPrint("[WFC] BSS %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n",
				bss->bssid[0], bss->bssid[1], bss->bssid[2], bss->bssid[3], bss->bssid[4], bss->bssid[5]);
			dietPrint("  %.*s\n", bss->ssid_len, bss->ssid);

			WlanAuthData auth = {};
			switch (slot->base.conn_type) {
				default:
				case WfcConnType_WepNormal: {
					switch (slot->base.wep_mode) {
						default:
						case WfcWepMode_Open:
							bss->auth_type = WlanBssAuthType_Open;
							break;
						case WfcWepMode_40bit:
							bss->auth_type = WlanBssAuthType_WEP_40;
							memcpy(auth.wep_key, slot->base.wep_keys[0], WLAN_WEP_40_LEN);
							break;
						case WfcWepMode_104bit:
							bss->auth_type = WlanBssAuthType_WEP_104;
							memcpy(auth.wep_key, slot->base.wep_keys[0], WLAN_WEP_104_LEN);
							break;
						case WfcWepMode_128bit:
							bss->auth_type = WlanBssAuthType_WEP_128;
							memcpy(auth.wep_key, slot->base.wep_keys[0], WLAN_WEP_128_LEN);
							break;
					}
					break;
				}

				case WfcConnType_WpaNormal: {
					bss->auth_type = (WlanBssAuthType)slot->wpa_mode;
					memcpy(auth.wpa_psk, slot->wpa_pmk, WLAN_WPA_PSK_LEN);
					break;
				}
			}

			wlmgrAssociate(bss, &auth);
			break;
		}

		case WfcPhase_SetupIP: {
			_wfcStartIP();

			if (s_wfcState.dhcp_active) {
				dietPrint("[WFC] dhcp started\n");
			} else {
				s_wfcState.cur_process = WfcProc_None;
				dietPrint("[WFC] connected!\n");
			}
			break;
		}
	}
}

static void _wfcRecv(void* user, NetBuf* pPacket)
{
	sgIP_memblock* mb = sgIP_memblock_alloc(2+pPacket->len);
	if (mb) {
		sgIP_memblock_exposeheader(mb, -2);
		memcpy(mb->datastart, netbufGet(pPacket), pPacket->len);
	}

	netbufFree(pPacket);

	if (mb) {
		SGIP_INTR_PROTECT();
		sgIP_Hub_ReceiveHardwarePacket(s_wfcState.iface, mb);
		SGIP_INTR_UNPROTECT();
	}
}

bool wfcInit(void)
{
	static bool initted = false;
	if (initted) {
		return true;
	}

	// Fail if WlMgr hasn't been previously initialized (this doesn't actually initialize it)
	if (!wlmgrInit(NULL, WLMGR_DEFAULT_THREAD_PRIO)) {
		return false;
	}

	sgIP_Init();
	s_wfcState.iface = sgIP_Hub_AddHardwareInterface(_wfcSend, NULL);
	s_wfcState.iface->flags &= ~SGIP_FLAG_HWINTERFACE_ENABLED;

	wlmgrSetEventHandler(_wfcOnEvent, NULL);
	wlmgrSetRawRxHandler(_wfcRecv, NULL);

	initted = true;
	return true;
}

void wfcClearConnSlots(void)
{
	s_wfcState.num_slots = 0;
}

void wfcLoadFromNvram(void)
{
	unsigned off_slots = g_envExtraInfo->nvram_offset_div8*8 - 4*sizeof(WfcConnSlot);

	s_wfcState.num_slots = 0;

	// Load DSi extended slots (if applicable)
	if (systemIsTwlMode()) {
		unsigned off_slots_ex = off_slots - 3*sizeof(WfcConnSlotEx);
		for (unsigned i = 0; i < 3; i ++) {
			WfcConnSlotEx* slot = &s_wfcSlots[s_wfcState.num_slots];
			if (!pmReadNvram(slot, off_slots_ex + i*sizeof(*slot), sizeof(*slot))) {
				dietPrint("[WFC] slot %u nvram read err\n", 1+i+3);
				continue;
			}

			if (slot->base.conn_type == WfcConnType_Invalid) {
				continue;
			}

			if (svcGetCRC16(0, &slot->base, offsetof(WfcConnSlot, crc16)) != slot->base.crc16) {
				dietPrint("[WFC] slot %u crc16.1 err\n", 1+i+3);
				continue;
			}

			if (svcGetCRC16(0, &slot->base+1, offsetof(WfcConnSlotEx, crc16) - sizeof(slot->base)) != slot->crc16) {
				dietPrint("[WFC] slot %u crc16.2 err\n", 1+i+3);
				continue;
			}

			if (!wfcLoadSlotEx(slot)) {
				dietPrint("[WFC] slot %u unsupported\n", 1+i+3);
			}
		}
	}

	// Load regular DS slots
	for (unsigned i = 0; i < 3; i ++) {
		WfcConnSlot* slot = &s_wfcSlots[s_wfcState.num_slots].base;
		if (!pmReadNvram(slot, off_slots + i*sizeof(*slot), sizeof(*slot))) {
			dietPrint("[WFC] slot %u nvram read err\n", 1+i);
			continue;
		}

		if (slot->conn_type == WfcConnType_Invalid) {
			continue;
		}

		if (svcGetCRC16(0, slot, offsetof(WfcConnSlot, crc16)) != slot->crc16) {
			dietPrint("[WFC] slot %u crc16 err\n", 1+i);
			continue;
		}

		if (!wfcLoadSlot(slot)) {
			dietPrint("[WFC] slot %u unsupported\n", 1+i);
		}
	}

	dietPrint("[WFC] loaded %u slots\n", s_wfcState.num_slots);
	for (unsigned i = 0; i < s_wfcState.num_slots; i ++) {
		WfcConnSlot* slot = &s_wfcSlots[i].base;
		(void)slot;
		dietPrint(" %u: ssid=%.*s\n", i, strnlen(slot->ssid, 32), slot->ssid);
	}
}

bool wfcLoadSlot(const WfcConnSlot* slot)
{
	if (slot->conn_type != WfcConnType_WepNormal) {
		return false;
	}

	if (s_wfcState.num_slots >= WFC_MAX_CONN_SLOTS) {
		return false;
	}

	WfcConnSlot* out = &s_wfcSlots[s_wfcState.num_slots++].base;
	if (slot != out) {
		memcpy(out, slot, sizeof(*out));
	}

	return true;
}

bool wfcLoadSlotEx(const WfcConnSlotEx* slot)
{
	if (slot->base.conn_type != WfcConnType_WepNormal && slot->base.conn_type != WfcConnType_WpaNormal) {
		return false;
	}

	if (s_wfcState.num_slots >= WFC_MAX_CONN_SLOTS) {
		return false;
	}

	WfcConnSlotEx* out = &s_wfcSlots[s_wfcState.num_slots++];
	if (slot != out) {
		memcpy(out, slot, sizeof(*out));
	}

	return true;
}

unsigned wfcGetNumSlots(void)
{
	return s_wfcState.num_slots;
}

static bool _wfcCanStartProcess(WlMgrState state)
{
	return s_wfcState.cur_process == WfcProc_None && (state == WlMgrState_Stopped || state == WlMgrState_Idle);
}

static bool _wfcStartProcess(WlMgrState state, WfcProc proc)
{
	s_wfcState.cur_process = proc;

	if (state == WlMgrState_Stopped) {
		wlmgrStart(WlMgrMode_Infrastructure);
	} else {
		_wfcOnEvent(NULL, WlMgrEvent_NewState, WlMgrState_Idle, WlMgrState_Idle);
	}

	return true;
}

bool wfcBeginScan(WlanBssScanFilter const* filter)
{
	WlMgrState state = wlmgrGetState();
	if (!_wfcCanStartProcess(state)) {
		return false;
	}

	s_wfcState.cur_bss = 0;
	s_wfcState.num_bss = 0;
	s_wfcState.scan.filter = filter;
	return _wfcStartProcess(state, WfcProc_Scan);
}

WlanBssDesc* wfcGetScanBssList(unsigned* out_count)
{
	if (s_wfcState.cur_process != WfcProc_None) {
		if (out_count) *out_count = 0;
		return NULL;
	}

	if (out_count) *out_count = s_wfcState.num_bss;
	return s_wfcBssDesc;
}

void _wpaDerivePmk(void* out, const void* ssid, unsigned ssid_len, const void* key, unsigned key_len);

bool wfcDeriveWpaKey(WlanAuthData* out, const char* ssid, unsigned ssid_len, const char* key, unsigned key_len)
{
	if (!systemIsTwlMode()) {
		return false;
	}

	_wpaDerivePmk(out->wpa_psk, ssid, ssid_len, key, key_len);
	return true;
}

bool wfcBeginAutoConnect(void)
{
	WlMgrState state = wlmgrGetState();
	if (!_wfcCanStartProcess(state) || s_wfcState.num_slots == 0) {
		return false;
	}

	s_wfcState.cur_slot = 0;
	return _wfcStartProcess(state, WfcProc_Connect);
}

bool wfcBeginConnect(WlanBssDesc const* bss, WlanAuthData const* auth)
{
	WlMgrState state = wlmgrGetState();
	if (!_wfcCanStartProcess(state)) {
		return false;
	}

	s_wfcState.cur_slot = WFC_MAX_CONN_SLOTS;
	s_wfcState.connect.bss = bss;
	s_wfcState.connect.auth = auth;
	return _wfcStartProcess(state, WfcProc_Connect);
}

WfcStatus wfcGetStatus(void)
{
	WlMgrState state = wlmgrGetState();

	if (state == WlMgrState_Scanning || s_wfcState.cur_process == WfcProc_Scan) {
		return WfcStatus_Scanning;
	} else if (s_wfcState.cur_process == WfcProc_Connect) {
		return s_wfcState.dhcp_active ? WfcStatus_AcquiringIP : WfcStatus_Connecting;
	} else if (state == WlMgrState_Associated) {
		return WfcStatus_Connected;
	} else {
		return WfcStatus_Disconnected;
	}
}

WfcConnSlot* wfcGetActiveSlot(void)
{
	if (s_wfcState.cur_process != WfcProc_None || wlmgrGetState() != WlMgrState_Associated || s_wfcState.cur_slot >= s_wfcState.num_slots) {
		return NULL;
	}

	return &s_wfcSlots[s_wfcState.cur_slot].base;
}

struct in_addr wfcGetIPConfig(struct in_addr* pGateway, struct in_addr* pSnmask, struct in_addr* pDns1, struct in_addr* pDns2)
{
	if (pGateway) {
		pGateway->s_addr = s_wfcState.iface->gateway;
	}

	if (pSnmask) {
		pSnmask->s_addr = s_wfcState.iface->snmask;
	}

	if (pDns1) {
		pDns1->s_addr = s_wfcState.iface->dns[0];
	}

	if (pDns2) {
		pDns2->s_addr = s_wfcState.iface->dns[1];
	}

	return (struct in_addr){ s_wfcState.iface->ipaddr };
}
