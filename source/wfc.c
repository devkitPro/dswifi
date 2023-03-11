#include <wfc.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include "sgIP/sgIP.h"

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
	bool is_connecting;
	bool dhcp_active;

	Mailbox ip_mbox;
} s_wfcState;

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
	// TODO
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
					s_wfcState.is_connecting = false;
					break;
				}

				default:
				case SGIP_DHCP_STATUS_IDLE:
				case SGIP_DHCP_STATUS_FAILED: {
					dietPrint("[IPThr] DHCP fail, giving up\n");
					wlmgrDeassociate();
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
	NetBuf* pPacket = netbufAlloc(8 + sizeof(NetLlcSnapHdr), mb->thislength, NetBufPool_Tx);
	if (pPacket) {
		memcpy(netbufGet(pPacket), mb->datastart, mb->thislength);
	}
	sgIP_memblock_free(mb);
	if (pPacket) {
		dietPrint("[WFC] tx len=%u\n", pPacket->len);
		wlmgrRawTx(pPacket);
	}
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
	WfcConnSlot* slot = &s_wfcSlots[s_wfcState.cur_slot].base;
	if (slot->ipv4_addr && slot->ipv4_gateway && slot->ipv4_subnet) {
		iface->ipaddr = slot->ipv4_addr;
		iface->gateway = slot->ipv4_gateway;
		iface->snmask = (1U << (32 - slot->ipv4_subnet)) - 1;
		s_wfcState.dhcp_active = false;
	} else {
		iface->ipaddr = htonl((169<<24) | (254<<16) | 2);
		iface->gateway = htonl((169<<24) | (254<<16) | 1);
		iface->snmask = 0xffff;
		s_wfcState.dhcp_active = true;
	}
	iface->dns[0] = slot->ipv4_dns[0];
	iface->dns[1] = slot->ipv4_dns[1];
	iface->flags |= SGIP_FLAG_HWINTERFACE_ENABLED;

	// Start sgIP thread
	threadPrepare(&s_wfcIPThread, _wfcIPThreadMain, NULL, &s_wfcIPThreadStack[sizeof(s_wfcIPThreadStack)], threadGetSelf()->baseprio);
	threadStart(&s_wfcIPThread);

	SGIP_INTR_PROTECT();
	if (s_wfcState.dhcp_active) {
		// Start DHCP!
		sgIP_DHCP_Start(iface, iface->dns[0] != 0);
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

			if (old_state >= WlMgrState_Associated) {
				dietPrint("[WFC] kicked out\n");
				_wfcStopIP();
			}

			if (old_state >= WlMgrState_Idle && new_state <= WlMgrState_Stopping) {
				dietPrint("[WFC] stopping\n");
				s_wfcState.is_connecting = false;
			}

			if (!s_wfcState.is_connecting) {
				break;
			}

			if (old_state == WlMgrState_Starting && new_state == WlMgrState_Idle) {
				s_wfcState.cur_slot = 0;
				phase = WfcPhase_Scan;
			} else if (old_state == WlMgrState_Scanning && new_state == WlMgrState_Idle) {
				if (s_wfcState.num_bss) {
					phase = WfcPhase_Connect;
				} else {
					s_wfcState.cur_slot ++;
					phase = WfcPhase_Scan;
				}
			} else if (old_state >= WlMgrState_Associating && new_state == WlMgrState_Idle) {
				s_wfcState.cur_bss ++;
				phase = WfcPhase_Connect;
			} else if (new_state == WlMgrState_Associated) {
				phase = WfcPhase_SetupIP;
			}

			break;
		}

		case WlMgrEvent_CmdFailed: {
			dietPrint("[WFC] cmd%u fail\n", arg0);
			s_wfcState.is_connecting = false;
			break;
		}

		case WlMgrEvent_ScanComplete: {
			s_wfcState.cur_bss = 0;
			s_wfcState.num_bss = arg1;
			dietPrint("[WFC] found %u\n", arg1);
			break;
		}

		case WlMgrEvent_Disconnected: {
			dietPrint("[WFC] deassoc reason %u\n", arg0);
			break;
		}
	}

	bool retry;
	do {
		retry = false;
		switch (phase) {
			default:
			case WfcPhase_None: break;

			case WfcPhase_Scan: {
				if (s_wfcState.cur_slot >= s_wfcState.num_slots) {
					dietPrint("[WFC] No more slots - FAIL\n");
					s_wfcState.is_connecting = false;
					break;
				}

				// Retrieve slot
				WfcConnSlotEx* slot = &s_wfcSlots[s_wfcState.cur_slot];
				unsigned ssidlen = strnlen(slot->base.ssid, 32);
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
				if (s_wfcState.cur_bss >= s_wfcState.num_bss) {
					dietPrint("[WFC] trying next slot\n");
					s_wfcState.cur_slot++;
					phase = WfcPhase_Scan;
					retry = true;
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
					s_wfcState.is_connecting = false;
					dietPrint("[WFC] connected!\n");
				}
				break;
			}
		}
	} while (retry);
}

static void _wfcRecv(void* user, NetBuf* pPacket)
{
	dietPrint("[WFC] rx len=%u\n", pPacket->len);
	sgIP_memblock* mb = sgIP_memblock_alloc(2+pPacket->len);
	if (mb) {
		sgIP_memblock_exposeheader(mb, -2);
		memcpy(mb->datastart, netbufGet(pPacket), pPacket->len);
		SGIP_INTR_PROTECT();
		sgIP_Hub_ReceiveHardwarePacket(s_wfcState.iface, mb);
		SGIP_INTR_UNPROTECT();
	}
	netbufFree(pPacket);
}

void wfcInit(void)
{
	sgIP_Init();
	s_wfcState.iface = sgIP_Hub_AddHardwareInterface(_wfcSend, NULL);
	s_wfcState.iface->flags &= ~SGIP_FLAG_HWINTERFACE_ENABLED;

	wlmgrSetEventHandler(_wfcOnEvent, NULL);
	wlmgrSetRawRxHandler(_wfcRecv, NULL);
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

			if (!slot->base.config) {
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

		if (!slot->config) {
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
		dietPrint(" %u: ssid=%.*s\n", i, strnlen(slot->ssid, 32), slot->ssid);
	}
}

bool wfcLoadSlot(const WfcConnSlot* slot)
{
	if (slot->conn_type != WfcConnType_WepNormal) {
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

void wfcBeginConnect(void)
{
	if (s_wfcState.is_connecting || s_wfcState.num_slots == 0) {
		return;
	}

	switch (wlmgrGetState()) {
		default: break;
		case WlMgrState_Stopped:
			s_wfcState.is_connecting = true;
			wlmgrStart(WlMgrMode_Infrastructure);
			break;
		case WlMgrState_Idle:
			s_wfcState.is_connecting = true;
			_wfcOnEvent(NULL, WlMgrEvent_NewState, WlMgrState_Idle, WlMgrState_Starting);
			break;
	}
}

WfcStatus wfcGetStatus(void)
{
	if (s_wfcState.is_connecting) {
		return WfcStatus_Connecting;
	}

	return wlmgrGetState() == WlMgrState_Associated ? WfcStatus_Connected : WfcStatus_Disconnected;
}

WfcConnSlot* wfcGetActiveSlot(void)
{
	if (wlmgrGetState() != WlMgrState_Associated || s_wfcState.is_connecting) {
		return NULL;
	}

	return &s_wfcSlots[s_wfcState.cur_slot].base;
}
