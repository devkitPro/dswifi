#include <dswifi9.h>
#include <wfc.h>
#include <unistd.h> // for gethostid

bool Wifi_InitDefault(bool useFirmwareSettings)
{
	if (!wlmgrInitDefault() || !wfcInit()) {
		return false;
	}

	if (useFirmwareSettings) {
		Wifi_AutoConnect();

		for (;;) {
			int status = Wifi_AssocStatus();
			if (status == ASSOCSTATUS_ASSOCIATED) break;
			if (status == ASSOCSTATUS_CANNOTCONNECT) return false;
			threadWaitForVBlank();
		}
	}

	return true;
}

void Wifi_AutoConnect(void)
{
	wfcClearConnSlots();
	wfcLoadFromNvram();
	wfcBeginConnect();
}

int Wifi_AssocStatus(void)
{
	return wfcGetStatus();
}

int Wifi_DisconnectAP(void)
{
	wlmgrDeassociate();
	return 0;
}

unsigned long Wifi_GetIP(void)
{
	return gethostid();
}

struct in_addr Wifi_GetIPInfo(struct in_addr* pGateway, struct in_addr* pSnmask, struct in_addr* pDns1, struct in_addr* pDns2)
{
	return wfcGetIPConfig(pGateway, pSnmask, pDns1, pDns2);
}
