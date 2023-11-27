/*
 * Copyright (c) 2022 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <psa/crypto.h>
#include <stdint.h>
#include "tfm_api.h"
#include "tfm_sp_log.h"
#include "nrfx_gpiote.h"
#include "hal/nrf_egu.h"
#include "hal/nrf_timer.h"
#include "hal/nrf_spim.h"
#include "psa/service.h"
#include "psa_manifest/tfm_secure_peripheral_partition.h"
#include <psa/crypto.h>
#include <psa/crypto_extra.h>
#include <psa/crypto_values.h>
#include <autoconf.h>
#include <zephyr/devicetree.h>
#include <mbedtls/ecdsa.h>
#include "mbedtls/aes.h"

struct tfm_pebble_service_init_args {
	uint8_t *buf;
	uint32_t len;
};
struct tfm_pebble_service_sign_args {
	uint8_t sign_buf[64];
};
psa_key_id_t key_id_out = 0;

static uint8_t m_hash[32];

static char str2Hex(char c)
{
    if (c >= '0' && c <= '9') {
        return (c - '0');
    }

    if (c >= 'a' && c <= 'z') {
        return (c - 'a' + 10);
    }

    if (c >= 'A' && c <= 'Z') {
        return (c -'A' + 10);
    }
    return c;
}

static int hexStr2Bin(char *str, size_t len,  char *bin) {
    int i,j;
    for(i = 0,j = 0; j < (len>>1) ; i++,j++)
    {
        bin[j] = (str2Hex(str[i]) <<4);
        i++;
        bin[j] |= str2Hex(str[i]);
    }
    return j; 
}

static int  cc3xx_decrypt(char *plain_text_decrypted,  char *cipher_text)
{

	mbedtls_aes_context ctx = {0};

	mbedtls_aes_init(&ctx);
	mbedtls_aes_setkey_dec_shadow_key( &ctx, 2, 128 );

	for (int i = 0; i < 4; i++) {
		mbedtls_internal_aes_decrypt(&ctx, cipher_text+(i<<4), plain_text_decrypted+(i<<4));
	}
	return 0;
}
static int sign_message(char *plain_text, uint32_t len, char *signature, int *sinlen)
{
	uint32_t output_len;
	psa_status_t status;

    int ret = 0, hash_length = 0;
    char sha256_output[32] = {0};

    status = psa_sign_message( key_id_out, PSA_ALG_ECDSA(PSA_ALG_SHA_256), plain_text, len, signature, *sinlen,  sinlen);
    if(status)
        return status;
    status = psa_verify_message( key_id_out, PSA_ALG_ECDSA(PSA_ALG_SHA_256), plain_text, len, signature, *sinlen);
	return status;
}
static int crypto_import_key(const uint8_t *data, size_t data_length, psa_key_id_t* key_id_out)
{
	psa_status_t status;
	uint8_t ecdsa_k1_encrypted_key[64];
	uint8_t ecdsa_k1_decrypted_key[64];
	uint8_t ecdsa_bin_key[32];
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

	hexStr2Bin(data, data_length,ecdsa_k1_encrypted_key);
	cc3xx_decrypt(ecdsa_k1_decrypted_key, ecdsa_k1_encrypted_key);
	hexStr2Bin(ecdsa_k1_decrypted_key,64,ecdsa_bin_key);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH);
	psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_K1));
	psa_set_key_bits(&attributes, 256);

	status = psa_import_key(&attributes, ecdsa_bin_key, 32, key_id_out);
	return status;
}
static void spp_crypto_key_init(void)
{
	psa_status_t status;
	psa_msg_t msg;
	uint8_t buf[12];
	uint32_t input_len;
	
	struct tfm_pebble_service_init_args pebble_input_key;
	struct tfm_pebble_service_init_args pebble_output_id;

	status = psa_get(TFM_SPP_INIT_SIGNAL, &msg);

	input_len = psa_read(msg.handle, 0, &pebble_input_key, sizeof(pebble_input_key));

	status = crypto_import_key(pebble_input_key.buf, pebble_input_key.len, &key_id_out);

	pebble_output_id.buf = buf;
	pebble_output_id.len = key_id_out;
	psa_write(msg.handle, 0, &pebble_output_id, sizeof(pebble_output_id));
	psa_reply(msg.handle, status);
}
static void spp_ecdsa_sign(void)
{
	psa_status_t status;
	psa_msg_t msg;
	uint8_t data_buf[100];
	uint32_t input_len;
	psa_key_id_t key_id_out = 0;
	struct tfm_pebble_service_init_args pebble_input_msg;
	struct tfm_pebble_service_sign_args pebble_output_sign;

	status = psa_get(TFM_SPP_SIGN_SIGNAL, &msg);

	input_len = psa_read(msg.handle, 0, &pebble_input_msg, sizeof(pebble_input_msg));

	input_len = msg.out_size[0];
	status = sign_message(pebble_input_msg.buf, pebble_input_msg.len, pebble_output_sign.sign_buf, &input_len);

	psa_write(msg.handle, 0, &pebble_output_sign, sizeof(pebble_output_sign));
	psa_reply(msg.handle, status);
}

static void spp_init(void)
{
	psa_crypto_init();
}

psa_status_t tfm_spp_main(void)
{
	psa_signal_t signals = 0;
	psa_status_t status;

	spp_init();
	while (1) {
		signals = psa_wait(PSA_WAIT_ANY, PSA_BLOCK);
		if (signals & TFM_SPP_INIT_SIGNAL) {
			spp_crypto_key_init();
		} else if (signals & TFM_SPP_SIGN_SIGNAL) {
			spp_ecdsa_sign();
		} else {
			psa_panic();
		}
	}

	return PSA_ERROR_SERVICE_FAILURE;
}
