#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain/common.h>
#include <zephyr/logging/log.h>

#if CONFIG_BOARD_THINGY91_NRF9160_NS
#include <tfm_ns_interface.h>
#include <tfm_ioctl_api.h>
#endif

#include <pm_config.h>

#include <psa/crypto.h>
#include <psa/crypto_extra.h>
#include <psa/crypto_values.h>

#include "ecdsa.h"
#include "sprout.h"
#include "nvs/local_storage.h"
#include "psa/crypto.h"

#include "include/utils/convert/convert.h"

LOG_MODULE_REGISTER(ecdsa, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define AES_KMU_SLOT              2
#define TEST_VERFY_SIGNATURE    
#define TV_NAME(name) name " -- [" __FILE__ ":" STRINGIFY(__LINE__) "]"
#define  CHECK_RESULT(exp_res,chk) \
        do \
        { \
            if(chk != exp_res){ \
                LOG_ERR("In function %s, line:%d, error_code:0x%04X \n", __func__,__LINE__,chk);\ 
            } \       
        } while (0)

#define ECDSA_MAX_INPUT_SIZE                (64)
#define ECPARAMS                            MBEDTLS_ECP_DP_SECP256R1
#define PUBKEY_BUF_ADDRESS(a)               (a + 80)
#define PUBKEY_STRIP_HEAD(a)                (a + 81)
#define KEY_BLOCK_SIZE                      190
#define MODEM_READ_HEAD_LEN                 200    
#define KEY_STR_BUF_SIZE                    329
#define MODEM_READ_BUF_SIZE                 (KEY_STR_BUF_SIZE + MODEM_READ_HEAD_LEN)
#define KEY_STR_LEN                         (KEY_STR_BUF_SIZE - 1)
#define PRIV_STR_BUF_SIZE                   133
#define PRIV_STR_LEN                        (PRIV_STR_BUF_SIZE - 1)
#define PUB_STR_BUF_SIZE                    197
#define KEY_HEX_SIZE                        184
#define PRIV_HEX_SIZE                       66
#define PUB_HEX_SIZE                        (KEY_HEX_SIZE - PRIV_HEX_SIZE)
#define PUB_HEX_ADDR(a)                     (a + PRIV_HEX_SIZE)
#define PUB_STR_ADDR(a)                     (a + PRIV_STR_LEN)
#define COM_PUB_STR_ADD(a)                  (a + PRIV_STR_LEN + 130)
#define UNCOM_PUB_STR_ADD(a)                (a + PRIV_STR_LEN)

#define APP_SUCCESS		(0)
#define APP_ERROR		(-1)

#define NRF_CRYPTO_EXAMPLE_ECDSA_TEXT_SIZE (100)

#define NRF_CRYPTO_EXAMPLE_ECDSA_PUBLIC_KEY_SIZE (65)
#define NRF_CRYPTO_EXAMPLE_ECDSA_SIGNATURE_SIZE (64)
#define NRF_CRYPTO_EXAMPLE_ECDSA_HASH_SIZE (32)


#define IOTEX_PAL_CRYPT_ECDSA_PUBLIC_KEY_SIZE       (130)

static psa_key_id_t _sign_keyid = 0;
static char _sign_pkey[130] = {0};

unsigned char* readECCPubKey(void);

#if 0
// uint16_t CRC16(uint8_t *data, size_t len) 
uint16_t iotex_pal_crypt_crc16(uint8_t *data, size_t len) 
{
    uint16_t crc = 0x0000;
    size_t j;
    int i;
    for (j = len; j > 0; j--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (i = 0; i < 8; i++) {
            if (crc & 0x8000) crc = (crc<<1) ^ 0x8005;
            else crc <<= 1;
        }
    }
    return (crc);
}
#endif

#if 0
/*
pub : ecc public key, sizeof pub not less than 129 bytes
*/
int get_ecc_public_key(char *pub) {
    unsigned char *pbuf = readECCPubKey();
    if (pbuf) {
        memcpy(pub, pbuf, 64);
        return 0;
    } else
        return -1;
}
#endif

unsigned char *readECCPubKey(void) {
    unsigned char buf[MODEM_READ_BUF_SIZE];
    static unsigned char pub[PUB_STR_BUF_SIZE];
    uint8_t *pbuf = ReadDataFromModem(ECC_KEY_SEC, buf, MODEM_READ_BUF_SIZE);
    memcpy(pub, UNCOM_PUB_STR_ADD(pbuf), 130);
    pub[130] = 0;
    return pub;
}

psa_key_id_t iotex_pal_crypt_get_signature_key_id(void)
{
    return _sign_keyid;
}

#ifdef IOTEX_PAL_CRYPT_USE_IOID
JWK * iotex_pal_crypt_init(void)
#else
int iotex_pal_crypt_init(void)
#endif
{
    uint8_t *pbuf;
    uint32_t ret = 0;
    char buf[MODEM_READ_BUF_SIZE] = {0};

#ifdef IOTEX_PAL_CRYPT_USE_IOID    
    JWK* _signJWK = NULL;
#endif

    psa_crypto_init();

    pbuf = ReadDataFromModem(ECC_KEY_SEC, buf, MODEM_READ_BUF_SIZE);
    if (pbuf) {

        memcpy(_sign_pkey, UNCOM_PUB_STR_ADD(pbuf), IOTEX_PAL_CRYPT_ECDSA_PUBLIC_KEY_SIZE);

#ifdef IOTEX_PAL_CRYPT_USE_IOID
        uint8_t secret[32] = {0};

        export_private(pbuf, 128, secret);

#ifdef IOTEX_SPROUT_SERVER_UES_TEST_NET
//        secret[31] = 0x5a;
#endif
        _signJWK = iotex_jwk_generate_by_secret(secret, sizeof(secret), 
                        JWKTYPE_EC, JWK_SUPPORT_KEY_ALG_K256, PSA_KEY_LIFETIME_VOLATILE, 
                        PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_MESSAGE | PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT, PSA_ALG_ECDSA(PSA_ALG_SHA_256), &_sign_keyid);    
        if (NULL == _signJWK) 
            LOG_INF("Fail to Generate a JWK for signature\n");
                
    }
    
    return _signJWK;
#else
		ret = export_psa_key(pbuf, 128, &_sign_keyid);
        // printf("Pubkey: %s\n", readECCPubKey());
        return ret;
    }
    else
        return 1;
#endif        
}

char *iotex_pal_crypt_ecdsa_public_key_export(void)
{
    if (_sign_pkey[0])
        return _sign_pkey;

    return NULL;
}

psa_status_t iotex_pal_crypt_ecdsa_sign(char *input, uint32_t input_length, char *sign, int *sign_length, bool isHash)
{
    psa_status_t status;

    if (NULL == input || NULL == sign || NULL == sign_length)
        return -1;

    if (0 == input_length)
        return -1;

    if (isHash && (input_length != 32))
        return -1;

#ifdef IOTEX_PAL_CRYPT_USE_SPP
    status = spp_sign(inbuf, len, buf, sinlen);
#else

    if (isHash)
        status = psa_sign_hash(_sign_keyid, PSA_ALG_ECDSA(PSA_ALG_SHA_256), input, input_length, sign, 64, sign_length);         
    else
        status = psa_sign_message(_sign_keyid, PSA_ALG_ECDSA(PSA_ALG_SHA_256), input, input_length, sign, 64, sign_length);     

#if 0
    status = psa_verify_message(_sign_keyid, PSA_ALG_ECDSA(PSA_ALG_SHA_256), input, input_length, sign, 64);
    printf("iotex_pal_crypt_ecdsa_sign verify ret %d\n", status);     
#endif

#endif

    return status;
}

#if 0
int safeRandom(void) {
    int  rand;
    psa_generate_random((uint8_t *)&rand, sizeof(rand));
    return rand;
}
#endif

psa_status_t iotex_pal_crypt_random_generate_string(char *out)
{
    uint8_t random_number_hex[8];

    psa_status_t status = psa_generate_random(random_number_hex, 8);
    if (PSA_SUCCESS == status)
        iotex_utils_convert_hex_to_str(random_number_hex, sizeof(random_number_hex), out);

    return status;
}

psa_status_t iotex_pal_crypt_random_generate(uint8_t *out, size_t out_size)
{
    if (NULL == out || 0 == out_size)
        return PSA_ERROR_INVALID_ARGUMENT;

    return psa_generate_random(out, out_size);
}

size_t iotex_pal_crypt_hash(uint32_t type, uint8_t *input, uint32_t input_length, uint8_t *hash_out, uint32_t hash_out_length)
{
    (void)type;
    
    size_t hash_length  = 0;
    psa_status_t status =  PSA_SUCCESS;

    if (NULL == input || NULL == hash_out)
        return PSA_ERROR_INVALID_ARGUMENT;

    if (0 == input_length)
        return PSA_ERROR_INVALID_ARGUMENT;

    if (hash_out_length < 32)
        return PSA_ERROR_INVALID_ARGUMENT;

    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;    
    psa_hash_setup(&operation, PSA_ALG_SHA_256);
    psa_hash_update(&operation, input, input_length);
    status = psa_hash_finish(&operation, hash_out, hash_out_length, &hash_length);    
    if (PSA_SUCCESS != status)  
        hash_length = 0;

    return hash_length;
}
