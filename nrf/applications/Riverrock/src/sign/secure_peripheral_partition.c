/*
 * Copyright (c) 2022 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <tfm_ns_interface.h>

#include "secure_peripheral_partition.h"

#include "psa/client.h"
#include "psa_manifest/sid.h"
#include <psa/crypto.h>
#include <psa/crypto_extra.h>
#include <psa/crypto_values.h>

psa_status_t export_psa_key(uint8_t* encrypted_key, uint32_t size,psa_key_id_t* key_id)
{
	psa_status_t status;
	psa_handle_t handle = TFM_SPP_INIT_HANDLE;
	struct tfm_pebble_service_init_args  input_args;
	struct tfm_pebble_service_init_args  output_args;
	psa_invec in_vec;
	psa_outvec out_vec;


	in_vec.base = (const void *)&input_args;
	in_vec.len = sizeof(input_args);

	input_args.buf = encrypted_key;
	input_args.len = size;

	out_vec.base = (const void *)&output_args;
	out_vec.len = sizeof(output_args);

	status = psa_call(handle, PSA_IPC_CALL,&in_vec,1,&out_vec,1);
	*key_id = output_args.len;

	return status;
}

psa_status_t spp_sign(char *plain_text, uint32_t len, char *signature, int *sinlen)
{
	psa_status_t status;
	psa_handle_t handle = TFM_SPP_SIGN_HANDLE;
	struct tfm_pebble_service_init_args  input_args;
	struct tfm_pebble_service_sign_args  output_args;
	psa_invec in_vec;
	psa_outvec out_vec;


	in_vec.base = (const void *)&input_args;
	in_vec.len = sizeof(input_args);

	input_args.buf = plain_text;
	input_args.len = len;

	out_vec.base = (const void *)&output_args;
	out_vec.len = sizeof(output_args);

	status = psa_call(handle, PSA_IPC_CALL,&in_vec,1,&out_vec,1);

	memcpy(signature, output_args.sign_buf, sizeof(output_args.sign_buf));
	
	return status;
}
