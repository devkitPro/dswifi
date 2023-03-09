#pragma once
#include <stdint.h>
#include <netinet/in.h>

static inline uint32_t htonl(uint32_t hostlong)
{
	return __builtin_bswap32(hostlong);
}

static inline uint16_t htons(uint16_t hostshort)
{
	return __builtin_bswap16(hostshort);
}

static inline uint32_t ntohl(uint32_t netlong)
{
	return __builtin_bswap32(netlong);
}

static inline uint16_t ntohs(uint16_t netshort)
{
	return __builtin_bswap16(netshort);
}

#ifdef __cplusplus
extern "C" {
#endif

uint32_t inet_addr(const char* cp);
int inet_aton(const char* cp, struct in_addr* inp);
char* inet_ntoa(struct in_addr in);

#ifdef __cplusplus
};
#endif
