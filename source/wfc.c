#include <wfc.h>
#include <stdalign.h>
#include <string.h>

alignas(32) static WfcConnSlotEx s_wfcSlots[WFC_MAX_CONN_SLOTS];
static unsigned s_wfcNumSlots;

void wfcInit(void)
{
}

void wfcClearConnSlots(void)
{
	s_wfcNumSlots = 0;
}

void wfcLoadFromNvram(void)
{
	unsigned off_slots = g_envExtraInfo->nvram_offset_div8*8 - 4*sizeof(WfcConnSlot);

	s_wfcNumSlots = 0;

	// Load DSi extended slots (if applicable)
	if (systemIsTwlMode()) {
		unsigned off_slots_ex = off_slots - 3*sizeof(WfcConnSlotEx);
		for (unsigned i = 0; i < 3; i ++) {
			WfcConnSlotEx* slot = &s_wfcSlots[s_wfcNumSlots];
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
		WfcConnSlot* slot = &s_wfcSlots[s_wfcNumSlots].base;
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

	dietPrint("[WFC] loaded %u slots\n", s_wfcNumSlots);
	for (unsigned i = 0; i < s_wfcNumSlots; i ++) {
		WfcConnSlot* slot = &s_wfcSlots[i].base;
		dietPrint(" %u: ssid=%.*s\n", i, strnlen(slot->ssid, 32), slot->ssid);
	}
}

bool wfcLoadSlot(const WfcConnSlot* slot)
{
	if (slot->conn_type != WfcConnType_WepNormal) {
		return false;
	}

	WfcConnSlot* out = &s_wfcSlots[s_wfcNumSlots++].base;
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

	WfcConnSlotEx* out = &s_wfcSlots[s_wfcNumSlots++];
	if (slot != out) {
		memcpy(out, slot, sizeof(*out));
	}

	return true;
}

unsigned wfcGetNumSlots(void)
{
	return s_wfcNumSlots;
}
