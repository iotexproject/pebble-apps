/*
 * Copyright (c) 2022 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <psa/crypto.h>
#include <psa/crypto_extra.h>
#include <psa/crypto_values.h>
#include "tfm_api.h"


struct tfm_pebble_service_init_args {
	uint8_t *buf;
	uint32_t len;
};

struct tfm_pebble_service_sign_args {
	uint8_t sign_buf[64];
};


psa_status_t export_psa_key(uint8_t* encrypted_key, uint32_t size,psa_key_id_t* key_id);

psa_status_t spp_sign(char *plain_text, uint32_t len, char *signature, int *sinlen);
