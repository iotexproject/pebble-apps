#ifndef    __ESDA_H__
#define    __ESDA_H__

int doESDA_sep256r_Sign(char *inbuf, uint32_t len, char *buf, int* sinlen);
void hex2str(char* buf_hex, int len, char *str);
int GenRandom(char *out);
int pebble_crypto_init(void);
int  get_ecc_public_key(char *pub);
int doESDASign(char *inbuf, uint32_t len, char *buf, int* sinlen);
int safeRandom(void);
uint16_t CRC16(uint8_t *data, size_t len);
int hexStr2Bin(char *str, char *bin);

#endif