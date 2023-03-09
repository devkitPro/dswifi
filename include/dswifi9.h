#pragma once
#if __has_include(<calico.h>)
#include <calico.h>
#elif __has_include(<nds.h>)
#include <nds.h>
#else
#error "Missing NDS platform lib"
#endif

#define WFC_CONNECT	true
#define INIT_ONLY	false

bool Wifi_InitDefault(bool useFirmwareSettings);
unsigned long Wifi_GetIP(void);
