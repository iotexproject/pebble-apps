#include <stdio.h>
#include <string.h>
#include <zephyr.h>
#include "ecdsa.h"
#include <drivers/entropy.h>
#include <sys/util.h>
#include <toolchain/common.h>
#include <logging/log.h>
#include "ecdsa.h"
#include "nvs/local_storage.h"


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

int get_ecc_key(void) {
    uint8_t *pbuf;
    char buf[MODEM_READ_BUF_SIZE];
    uint8_t decrypted_buf[PRIV_STR_BUF_SIZE];
    uint8_t binBuf[PRIV_STR_BUF_SIZE];
    unsigned char pub[PUB_STR_BUF_SIZE];

    memset(buf, 0, sizeof(buf));
    pbuf = ReadDataFromModem(ECC_KEY_SEC, buf, MODEM_READ_BUF_SIZE);
    if (pbuf) {
        memcpy(decrypted_buf, pbuf, PRIV_STR_LEN);
        decrypted_buf[PRIV_STR_LEN] = 0;
        hexStr2Bin(decrypted_buf, binBuf);
        for (int i = 0; i < 4; i++) {
            if (cc3xx_decrypt(AES_KMU_SLOT, decrypted_buf+(i<<4), binBuf+(i<<4))) {
                LOG_ERR("cc3xx_decrypt erro\n");
                return -1;
            }
        }
        decrypted_buf[64] = 0;
#ifdef TEST_VERFY_SIGNATURE    
        memcpy(pub, UNCOM_PUB_STR_ADD(pbuf)+2, 128);
        pub[128] = 0;
        SetEccPrivKey(decrypted_buf, pub);
        LOG_INF("pub:%s \n", pub);
#else
        memcpy(pub, UNCOM_PUB_STR_ADD(pbuf), 130);
        pub[130] = 0;
        LOG_INF("pub:%s \n", pub);
        SetEccPrivKey(decrypted_buf);
#endif            
    }
    else
        return 1;
    return  0;
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

int startup_check_ecc_key(void) {
    return get_ecc_key() ? -4 : 0;
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

int doESDASign(char *inbuf, uint32_t len, char *buf, int *sinlen) {
    int ret = 0;
#ifdef TEST_VERFY_SIGNATURE    
    __disable_irq();
    ret = doESDA_sep256r_Sign(inbuf, len, buf, sinlen);
    __enable_irq();
    LOG_INF("doESDA_sep256r_Sign return :%x \n", ret);
#else
    doESDA_sep256r_Sign(inbuf, len, buf, sinlen);
#endif
    LowsCalc(buf+32, buf+32);
    return ret;
}

int safeRandom(void) {
    int  rand;
    __disable_irq();
    rand = iotex_random();
    __enable_irq();
    return rand;
}
