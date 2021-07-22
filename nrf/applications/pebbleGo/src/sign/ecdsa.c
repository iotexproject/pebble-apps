#include <stdio.h>
#include <string.h>
#include <zephyr.h>
#include "ecdsa.h"
#include <drivers/entropy.h>
#include <sys/util.h>
#include <toolchain/common.h>
#include "ecdsa.h"
#include "nvs/local_storage.h"

#define   AES_KMU_SLOT			  2

//#define	  TEST_VERFY_SIGNATURE	

#define TV_NAME(name) name " -- [" __FILE__ ":" STRINGIFY(__LINE__) "]"
#define  CHECK_RESULT(exp_res,chk) \
         do \
         { \
            if(chk != exp_res){ \
                printk("In function %s, line:%d, error_code:0x%04X \n", __func__,__LINE__,chk);\ 
            } \   	
         } while (0)
         
#define ECDSA_MAX_INPUT_SIZE (64)

#define ECPARAMS    MBEDTLS_ECP_DP_SECP256R1

#define PUBKEY_BUF_ADDRESS(a)  (a+80)

#define PUBKEY_STRIP_HEAD(a)   (a+81)

#define  KEY_BLOCK_SIZE 190

static uint16_t CRC16(uint8_t *data, size_t len) {
	uint16_t crc = 0x0000;
	size_t j;
	int i;
	for (j=len; j>0; j--) {
		crc ^= (uint16_t)(*data++) << 8;
		for (i=0; i<8; i++) {
			if (crc & 0x8000) crc = (crc<<1) ^ 0x8005;
			else crc <<= 1;
		}
	}
	return (crc);
}

int get_ecc_key(void)
{
	char buf[KEY_BLOCK_SIZE];
	uint8_t decrypted_buf[66];
	iotex_local_storage_load(SID_ECC_KEY,buf,sizeof(buf));

	for(int i = 0; i <4; i++){
		if(cc3xx_decrypt(AES_KMU_SLOT, decrypted_buf+(i<<4), buf+(i<<4))){
			printk("cc3xx_decrypt erro\n");
			return -1;				
		}
	}
	decrypted_buf[64] = 0;
	//printk("decrypted : 0x%x, 0x%x,0x%x,0x%x\n", decrypted_buf[0],decrypted_buf[1],decrypted_buf[2],decrypted_buf[3]);
#ifdef	TEST_VERFY_SIGNATURE	
	unsigned char pub[160];
	hex2str(PUBKEY_STRIP_HEAD(buf), 64, pub);
	printk("buf:");
	for(int j =0; j < 64; j++)
	{
		printk("%02x", (char)*(PUBKEY_BUF_ADDRESS(buf)+j));
	}
	printk("\n");
	printk("\n");	
	printk("pu_compressed:");
	for(int j = 0; j <33; j++ )
	{
		printk("%02x", (PUBKEY_BUF_ADDRESS(buf)+65)[j]);
	}
	printk("\n");	
	printk("\n");
	printk("pub:%s\n", pub);
	printk("priv:%s\n", decrypted_buf);
    SetEccPrivKey(decrypted_buf, pub);
#else
	SetEccPrivKey(decrypted_buf);
#endif
	return  0;
}
/*
pub : ecc public key, sizeof pub not less than 129 bytes
*/
int  get_ecc_public_key(char *pub)
{
	char buf[KEY_BLOCK_SIZE];
	static unsigned char trans_key = 2  /*0*/ ;
	if(pub == NULL){
		return  trans_key;
	}
	else {
		if(trans_key > 0){
			iotex_local_storage_load(SID_ECC_KEY,buf,sizeof(buf));	
			hex2str(PUBKEY_STRIP_HEAD(buf), 64, pub);
			trans_key--;
			return 1;
		}
		else
			return 0;
	}	
}

unsigned char* readECCPubKey(void)
{
	unsigned char buf[KEY_BLOCK_SIZE];
	static unsigned char pub[160];
	iotex_local_storage_load(SID_ECC_KEY,buf,sizeof(buf));	
	hex2str(PUBKEY_BUF_ADDRESS(buf)+65, 33, pub);
	return pub;
}

/*
	generate AES key, ecc key
	buf[160] : the index of  0 ~ 79  used to store private key(hex-string),  80 ~ 179  used to store public key (binary qx,qy)
*/
int startup_check_ecc_key(void)
{
	char buf[KEY_BLOCK_SIZE];	
	uint8_t key[16];
	uint8_t encrypted_buf[66];
	uint16_t crc;
	int ret,i;

	uint8_t read[16];

	memset(buf, 0, sizeof(buf));	
	iotex_local_storage_load(SID_ECC_KEY,buf,sizeof(encrypted_buf));	
	crc = *(uint16_t *)(buf+64);
	printk("crc :0x%x\n", crc);	
	if((CRC16(buf, 64) != crc) || (!crc))
	//if(1)
	{
		int  ret_len1 = sizeof(buf), ret_len2 = 80;
		if((ret = gen_ecc_key(buf,&ret_len1, PUBKEY_BUF_ADDRESS(buf), &ret_len2))==0)
		{				
			printk("ret_len1:%d,ret_len2:%d, cmp: %d \n", ret_len1, ret_len2&0x0000FFFF, (ret_len2&0xFFFF0000)>>16);
			ret = (ret_len2&0xFFFF0000)>>16;
			for(i = 0; i <ret; i++ )
			{
				printk("%02x", (PUBKEY_BUF_ADDRESS(buf)+(ret_len2&0x0000FFFF))[i]);
			}
			printk("\n");

			genAESKey(key, sizeof(key));
			ret = store_key_in_kmu(AES_KMU_SLOT, key, read);
			if(ret)	{
				printk("write key into kmu slot:%d erro:%d\n",AES_KMU_SLOT,ret);
				return -1;
			}
			//printk("To be encrypt_priv :0x%x,0x%x,0x%x,0x%x\n", buf[0],buf[1],buf[2],buf[3]);
			//printk("To be encrypt_pub :0x%x,0x%x,0x%x,0x%x\n", PUBKEY_BUF_ADDRESS(buf)[0],PUBKEY_BUF_ADDRESS(buf)[1],PUBKEY_BUF_ADDRESS(buf)[2],PUBKEY_BUF_ADDRESS(buf)[3]);

			for(i = 0; i <4; i++){
				if(ret = cc3xx_encrypt(AES_KMU_SLOT, buf+(i<<4), encrypted_buf+(i<<4))){
					printk("cc3xx_encrypt erro: 0x%x\n", ret);
					return -2;						
				}
			}
#if 0				
			buf[0]=0;buf[1]=0;buf[2]=0;buf[3]=0;			
			for(i = 0; i <4; i++){
				if(ret = cc3xx_decrypt(AES_KMU_SLOT, buf+(i<<4), encrypted_buf+(i<<4))){
					printk("cc3xx_encrypt erro: 0x%x\n", ret);
					return -2;						
				}
			}
			printk("Dencrypted :0x%x,0x%x,0x%x,0x%x\n", buf[0],buf[1],buf[2],buf[3]);
			return -1;
#endif
			crc = CRC16(encrypted_buf, 64);			
			*(uint16_t *)(encrypted_buf+64) = crc;
			memcpy(buf, encrypted_buf, sizeof(encrypted_buf));				
			iotex_local_storage_save(SID_ECC_KEY,buf, sizeof(buf));
		}
		else {
			printk("gen ecc return : 0x%x \n", ret);
			return -3;
		}			
	}
	if(get_ecc_key())
	{
		return -4;
	}
	return  0;
}


void hex2str(char* buf_hex, int len, char *str)
{
	int i,j;
    const char hexmap[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };	
	for(i =0,j=0; i< len; i++)
	{		
		str[j++] = hexmap[buf_hex[i]>>4];	
		str[j++] = hexmap[buf_hex[i]&0x0F];
	}
	str[j] = 0;	
}

int doESDASign(char *inbuf, uint32_t len, char *buf, int* sinlen)
{
	int ret;
	//ret = doESDA_sep256r_Sign(inbuf, len, buf, sinlen);
	doESDA_sep256r_Sign(inbuf, len, buf, sinlen);
	LowsCalc(buf+32, buf+32);
	return ret;
}

