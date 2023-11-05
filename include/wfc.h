#pragma once
#include <calico.h>
#include <netinet/in.h>

#define WFC_MAX_CONN_SLOTS 6

typedef enum WfcStatus {
	WfcStatus_Disconnected = 0,
	WfcStatus_Scanning     = 1,
	WfcStatus_Connecting   = 2,
	WfcStatus_AcquiringIP  = 3,
	WfcStatus_Connected    = 4,
} WfcStatus;

typedef enum WfcWepMode {
	WfcWepMode_Open   = 0,
	WfcWepMode_40bit  = 1,
	WfcWepMode_104bit = 2,
	WfcWepMode_128bit = 3,
} WfcWepMode;

typedef enum WfcConnType {
	WfcConnType_WepNormal = 0x00,
	WfcConnType_WepAoss   = 0x01,
	WfcConnType_WpaNormal = 0x10,
	WfcConnType_WpaWps    = 0x13,
	WfcConnType_Invalid   = 0xff,
} WfcConnType;

typedef enum WfcWpaMode {
	WfcWpaMode_Invalid       = 0,
	WfcWpaMode_WPA_PSK_TKIP  = 4,
	WfcWpaMode_WPA2_PSK_TKIP = 5,
	WfcWpaMode_WPA_PSK_AES   = 6,
	WfcWpaMode_WPA2_PSK_AES  = 7,
} WfcWpaMode;

typedef struct WfcConnSlot {
	char proxy_user[32];
	char proxy_password[32];
	char ssid[32];
	char aoss_ssid[32];
	u8   wep_keys[4][16];
	u32  ipv4_addr;
	u32  ipv4_gateway;
	u32  ipv4_dns[2];
	u8   ipv4_subnet;
	u8   wep_keys_aoss[4][5];
	u8   _pad_0xe5;
	u8   wep_mode     : 2; // WfcWepMode
	u8   wep_is_ascii : 1;
	u8   _pad_0xe6    : 5;
	u8   conn_type;        // WfcConnType
	u16  ssid_len;
	u16  mtu;
	u8   _pad_0xec[3];
	u8   config;
	u8   wfc_user_id[14];
	u16  crc16;
} WfcConnSlot;

typedef struct WfcConnSlotEx {
	WfcConnSlot base;
	u8   wpa_pmk[32];
	char wpa_psk[64];
	u8   _pad_0x160[33];
	u8   wpa_mode;       // WfcWpaMode
	u8   proxy_enable;
	u8   proxy_has_auth;
	char proxy_name[48];
	u8   _pad_0x1b4[52];
	u16  proxy_port;
	u8   _pad_0x1ea[20];
	u16  crc16;
} WfcConnSlotEx;

#ifdef __cplusplus
extern "C" {
#endif

bool wfcInit(void);

void wfcClearConnSlots(void);
void wfcLoadFromNvram(void);
bool wfcLoadSlot(const WfcConnSlot* slot);
bool wfcLoadSlotEx(const WfcConnSlotEx* slot);
unsigned wfcGetNumSlots(void);

bool wfcBeginScan(WlanBssScanFilter const* filter);
WlanBssDesc* wfcGetScanBssList(unsigned* out_count);

bool wfcDeriveWpaKey(WlanAuthData* out, const char* ssid, unsigned ssid_len, const char* key, unsigned key_len);

bool wfcBeginAutoConnect(void);
bool wfcBeginConnect(WlanBssDesc const* bss, WlanAuthData const* auth);
WfcStatus wfcGetStatus(void);
WfcConnSlot* wfcGetActiveSlot(void);
struct in_addr wfcGetIPConfig(struct in_addr* pGateway, struct in_addr* pSnmask, struct in_addr* pDns1, struct in_addr* pDns2);

#ifdef __cplusplus
}
#endif
