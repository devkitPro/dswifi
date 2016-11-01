// DS Wifi interface code
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// wifi_arm9.c - arm9 wifi support header
/****************************************************************************** 
DSWifi Lib and test materials are licenced under the MIT open source licence:
Copyright (c) 2005-2006 Stephen Stair

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
******************************************************************************/


#ifndef WIFI_ARM9_H
#define WIFI_ARM9_H

#include <nds.h>
#include "wifi_shared.h"

//////////////////////////////////////////////////////////////////////////
// wifi heap allocator system

#define WHEAP_RECORD_FLAG_INUSE     0
#define WHEAP_RECORD_FLAG_UNUSED    1
#define WHEAP_RECORD_FLAG_FREED     2

typedef struct WHEAP_RECORD {
    struct WHEAP_RECORD * next;
    unsigned short flags, unused;
    int size;
} wHeapRecord;

#ifdef SGIP_DEBUG
#define WHEAP_FILL_START    0xAA
#define WHEAP_FILL_END      0xBB
#define WHEAP_PAD_START     4
#define WHEAP_PAD_END       4
#define WHEAP_DO_PAD
#else
#define WHEAP_PAD_START     0
#define WHEAP_PAD_END       0
#undef WHEAP_DO_PAD
#endif
#define WHEAP_RECORD_SIZE   (sizeof(wHeapRecord))
#define WHEAP_PAD_SIZE      ((WHEAP_PAD_START)+(WHEAP_PAD_END))
#define WHEAP_SIZE_CUTOFF   ((WHEAP_RECORD_SIZE)+64)




#endif



#ifdef __cplusplus
extern "C" {
#endif

extern volatile Wifi_MainStruct * WifiData;
extern void Wifi_CopyMacAddr(volatile void * dest, volatile void * src);
extern int Wifi_CmpMacAddr(volatile void * mac1, volatile void * mac2);

extern void * sgIP_malloc(int size);
extern void sgIP_free(void * ptr);

#ifdef __cplusplus
}
#endif
