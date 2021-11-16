#ifndef	__ESDA_H__
#define	__ESDA_H__

typedef struct
{
	int flag;
	char RSA_N[513];
	char RSA_E[7];
	char RSA_D[513];
	char RSA_P[257];
	char RSA_Q[257];
	char RSA_DP[257];
	char RSA_DQ[257];
	char RSA_QP[257];
}RSA_KEY_PAIR;

int initECDSA_sep256r(void);
int doESDA_sep256r_Sign(char *inbuf, uint32_t len, char *buf, int* sinlen);
void hex2str(char* buf_hex, int len, char *str);
int GenRandom(char *out);
int RSA_decrypt(char *input, int len, char *out, int out_len);
bool RSAPubNeedUpload(void);
int CheckPublickKey(void);
int  GetRSAkeypair(RSA_KEY_PAIR *pkey);

#endif