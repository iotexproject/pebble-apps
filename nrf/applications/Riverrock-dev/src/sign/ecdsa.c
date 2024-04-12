#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include "ecdsa.h"
#include <zephyr/drivers/entropy.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain/common.h>
#include <zephyr/logging/log.h>
#if CONFIG_BOARD_THINGY91_NRF9160_NS
#include <tfm_ns_interface.h>
#include <tfm_ioctl_api.h>
#endif
#include <psa/crypto.h>
#include <psa/crypto_extra.h>
#include <psa/crypto_values.h>
#include <pm_config.h>
#include "ecdsa.h"
#include "nvs/local_storage.h"

#include "psa/crypto.h"

LOG_MODULE_REGISTER(ecdsa, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define   AES_KMU_SLOT              2
#define      TEST_VERFY_SIGNATURE    
#define TV_NAME(name) name " -- [" __FILE__ ":" STRINGIFY(__LINE__) "]"
#define  CHECK_RESULT(exp_res,chk) \
        do \
        { \
            if(chk != exp_res){ \
                LOG_ERR("In function %s, line:%d, error_code:0x%04X \n", __func__,__LINE__,chk);\ 
            } \       
        } while (0)
#define ECDSA_MAX_INPUT_SIZE (64)
#define ECPARAMS    MBEDTLS_ECP_DP_SECP256R1
#define PUBKEY_BUF_ADDRESS(a)  (a+80)
#define PUBKEY_STRIP_HEAD(a)   (a+81)
#define  KEY_BLOCK_SIZE 190
#define   MODEM_READ_HEAD_LEN    200    
#define   KEY_STR_BUF_SIZE        329
#define   MODEM_READ_BUF_SIZE    (KEY_STR_BUF_SIZE+MODEM_READ_HEAD_LEN)
#define   KEY_STR_LEN    (KEY_STR_BUF_SIZE-1)
#define      PRIV_STR_BUF_SIZE        133
#define   PRIV_STR_LEN           (PRIV_STR_BUF_SIZE-1)
#define   PUB_STR_BUF_SIZE        197
#define  KEY_HEX_SIZE        184
#define  PRIV_HEX_SIZE        66
#define  PUB_HEX_SIZE        (KEY_HEX_SIZE - PRIV_HEX_SIZE)
#define  PUB_HEX_ADDR(a)    (a+PRIV_HEX_SIZE)
#define  PUB_STR_ADDR(a)    (a+PRIV_STR_LEN)
#define  COM_PUB_STR_ADD(a)  (a+PRIV_STR_LEN+130)
#define  UNCOM_PUB_STR_ADD(a) (a+PRIV_STR_LEN)

#define APP_SUCCESS		(0)
#define APP_ERROR		(-1)

#define NRF_CRYPTO_EXAMPLE_ECDSA_TEXT_SIZE (100)

#define NRF_CRYPTO_EXAMPLE_ECDSA_PUBLIC_KEY_SIZE (65)
#define NRF_CRYPTO_EXAMPLE_ECDSA_SIGNATURE_SIZE (64)
#define NRF_CRYPTO_EXAMPLE_ECDSA_HASH_SIZE (32)

psa_key_id_t key_id;

unsigned char* readECCPubKey(void);

uint16_t CRC16(uint8_t *data, size_t len) {
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

unsigned char *readECCPubKey(void) {
    unsigned char buf[MODEM_READ_BUF_SIZE];
    static unsigned char pub[PUB_STR_BUF_SIZE];
    uint8_t *pbuf = ReadDataFromModem(ECC_KEY_SEC, buf, MODEM_READ_BUF_SIZE);
    memcpy(pub, UNCOM_PUB_STR_ADD(pbuf), 130);
    pub[130] = 0;
    return pub;
}

int pebble_crypto_init(void) {
    uint8_t *pbuf;
    uint32_t ret = 0;
    char buf[MODEM_READ_BUF_SIZE];

    psa_crypto_init();
    memset(buf, 0, sizeof(buf));
    pbuf = ReadDataFromModem(ECC_KEY_SEC, buf, MODEM_READ_BUF_SIZE);
    if (pbuf){
		ret = export_psa_key(pbuf, 128, &key_id);
        printf("Pubkey: %s\n", readECCPubKey());
        return ret;
    }
    else
        return 1;
}

void hex2str(char* buf_hex, int len, char *str) {
    int i, j;
    const char hexmap[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };    

    for (i = 0, j = 0; i < len; i++) {
        str[j++] = hexmap[buf_hex[i] >> 4];    
        str[j++] = hexmap[buf_hex[i] & 0x0F];
    }
    str[j] = 0;    
}
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

int hexStr2Bin(char *str, char *bin) {
    int i,j;
    for(i = 0,j = 0; j < (strlen(str)>>1) ; i++,j++)
    {
        bin[j] = (str2Hex(str[i]) <<4);
        i++;
        bin[j] |= str2Hex(str[i]);
    }   
    return j; 
}
int doESDASign(char *inbuf, uint32_t len, char *buf, int *sinlen) {
    int ret = 0;

    ret = spp_sign(inbuf, len, buf, sinlen);
    return ret;
}

int safeRandom(void) {
    int  rand;
    psa_generate_random((uint8_t *)&rand, sizeof(rand));
    return rand;
}
int GenRandom(char *out)
{
    psa_status_t status;
    uint8_t random_number_hex[8];
    status = psa_generate_random(random_number_hex, 8);
    hex2str(random_number_hex,sizeof(random_number_hex), out);
    return status;
}

