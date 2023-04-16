#pragma once
#include <calico.h>
#include <netinet/in.h>

#define WFC_CONNECT	true
#define INIT_ONLY	false

enum WIFI_ASSOCSTATUS {
	ASSOCSTATUS_DISCONNECTED,  // not *trying* to connect
	ASSOCSTATUS_SEARCHING,     // data given does not completely specify an AP, looking for AP that matches the data.
	ASSOCSTATUS_ASSOCIATING,   // connecting...
	ASSOCSTATUS_ACQUIRINGDHCP, // connected to AP, but getting IP data from DHCP
	ASSOCSTATUS_ASSOCIATED,    // Connected! (COMPLETE if Wifi_ConnectAP was called to start)

	ASSOCSTATUS_CANNOTCONNECT  = ASSOCSTATUS_DISCONNECTED, // error in connecting... (COMPLETE if Wifi_ConnectAP was called to start)
	ASSOCSTATUS_AUTHENTICATING = ASSOCSTATUS_ASSOCIATING,  // connecting...
};

#ifdef __cplusplus
extern "C" {
#endif

bool Wifi_InitDefault(bool useFirmwareSettings);

void Wifi_AutoConnect(void);
int Wifi_AssocStatus(void);
int Wifi_DisconnectAP(void);

unsigned long Wifi_GetIP(void);
struct in_addr Wifi_GetIPInfo(struct in_addr* pGateway, struct in_addr* pSnmask, struct in_addr* pDns1, struct in_addr* pDns2);

#ifdef __cplusplus
}
#endif
