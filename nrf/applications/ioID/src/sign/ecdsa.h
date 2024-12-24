#ifndef    __ESDA_H__
#define    __ESDA_H__

#include <stdbool.h>

#include "psa/crypto.h"
#include "include/jose/jose.h"

#define IOTEX_PAL_CRYPT_USE_IOID

#ifdef IOTEX_PAL_CRYPT_USE_IOID
JWK * iotex_pal_crypt_init(void);
#else
int iotex_pal_crypt_init(void);
#endif

psa_status_t iotex_pal_crypt_random_generate_string(char *out);
psa_status_t iotex_pal_crypt_random_generate(uint8_t *out, size_t out_size);
psa_status_t iotex_pal_crypt_ecdsa_sign(char *input, uint32_t input_length, char *sign, int *sign_length, bool isHash);
size_t iotex_pal_crypt_hash(uint32_t type, uint8_t *input, uint32_t input_length, uint8_t *hash_out, uint32_t hash_out_length);
char *iotex_pal_crypt_ecdsa_public_key_export(void);

#endif