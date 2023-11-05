#include <string.h>
#include <calico/types.h>
#include <calico/dev/wlan.h>
#include <calico/nds/bios.h>

static void _wpaHmacSha1(void* out, const void* key, size_t key_len, const void* data, size_t data_len)
{
	SvcSha1Context ctx;
	ctx.hash_block = NULL;

	u8 keyblock[64];
	const u8* keyp = (const u8*)key;

	// Clamp down key length when larger than block size
	if (key_len > sizeof(keyblock)) {
		svcSha1CalcTWL(keyblock, key, key_len);
		keyp = keyblock;
		key_len = SVC_SHA1_DIGEST_SZ;
	}

	// Copy key (zeropadded) and XOR it with 0x36
	size_t i;
	for (i = 0; i < key_len; i ++) {
		keyblock[i] = keyp[i] ^ 0x36;
	}
	for (; i < sizeof(keyblock); i ++) {
		keyblock[i] = 0x36;
	}

	// Inner hash
	svcSha1InitTWL(&ctx);
	svcSha1UpdateTWL(&ctx, keyblock, sizeof(keyblock));
	svcSha1UpdateTWL(&ctx, data, data_len);
	svcSha1DigestTWL(out, &ctx);

	// Convert inner key into outer key
	for (i = 0; i < sizeof(keyblock); i ++) {
		keyblock[i] ^= 0x36 ^ 0x5c;
	}

	// Outer hash
	svcSha1InitTWL(&ctx);
	svcSha1UpdateTWL(&ctx, keyblock, sizeof(keyblock));
	svcSha1UpdateTWL(&ctx, out, SVC_SHA1_DIGEST_SZ);
	svcSha1DigestTWL(out, &ctx);
}

static void _wpaPseudoRandomFunction(void* out, size_t out_len, const void* key, size_t key_len, void* pad, size_t pad_len, unsigned num_rounds)
{
	u8 round_dst[SVC_SHA1_DIGEST_SZ];
	u8 round_src[SVC_SHA1_DIGEST_SZ];
	u8 round_sum[SVC_SHA1_DIGEST_SZ];
	u8* pCounter = (u8*)pad + pad_len - 1;

	while (out_len) {
		size_t cur_sz = out_len > SVC_SHA1_DIGEST_SZ ? SVC_SHA1_DIGEST_SZ : out_len;
		_wpaHmacSha1(round_dst, key, key_len, pad, pad_len);

		for (unsigned i = 0; i < SVC_SHA1_DIGEST_SZ; i ++) {
			round_sum[i] = round_dst[i];
		}

		for (unsigned round = 1; round < num_rounds; round ++) {
			for (unsigned i = 0; i < SVC_SHA1_DIGEST_SZ; i ++) {
				round_src[i] = round_dst[i];
			}

			_wpaHmacSha1(round_dst, key, key_len, round_src, SVC_SHA1_DIGEST_SZ);

			for (unsigned i = 0; i < SVC_SHA1_DIGEST_SZ; i ++) {
				round_sum[i] ^= round_dst[i];
			}
		}

		(*pCounter)++;

		memcpy(out, round_sum, cur_sz);
		out = (u8*)out + cur_sz;
		out_len -= cur_sz;
	}
}

void _wpaDerivePmk(void* out, const void* ssid, unsigned ssid_len, const void* key, unsigned key_len)
{
	if (ssid_len > WLAN_MAX_SSID_LEN) {
		ssid_len = WLAN_MAX_SSID_LEN;
	}

	u8 pad[WLAN_MAX_SSID_LEN+4];
	memcpy(pad, ssid, ssid_len);
	pad[ssid_len+0] = 0;
	pad[ssid_len+1] = 0;
	pad[ssid_len+2] = 0;
	pad[ssid_len+3] = 1;

	_wpaPseudoRandomFunction(out, WLAN_WPA_PSK_LEN, key, key_len, pad, ssid_len+4, 4096);
}
