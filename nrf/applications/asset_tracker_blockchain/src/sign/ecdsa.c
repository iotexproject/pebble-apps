#include <stdio.h>
#include <zephyr.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ctr_drbg.h>
#include "ecdsa.h"
#include <drivers/entropy.h>
#include <sys/util.h>
#include <toolchain/common.h>
#include "ecdsa.h"
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#define KEY_SIZE 2048
#define EXPONENT 65537

//#define  DEBUG_IOTEX_CRYPTO   
#ifdef DEBUG_IOTEX_CRYPTO
#define  IOTEX_CRYPTO_DEBUG_OUTPUT(...)  printk(__VA_ARGS__)
#else
#define  IOTEX_CRYPTO_DEBUG_OUTPUT(...)  
#endif

const char RSA_N[]="B4CB45541BBBE9CF3DC64D33F6E0E3F922472EE09992A47D540A220B11C022C81D9CFEEE16611C422C629E66D81661B3A1A3D8464A57D61DAEDF7CEB85B2132D33355C54D8E1FBA9D751F70BC9FDA355D008BBB94CCBE64F5EA4658EA4907614D77447616FB3D1A903FB8285596446EDF5FF830F2BE657F0DAF4F63E0C2B922C48C5DE828D9A4642D875385FEE431170F60DC6E98ED7FD50FC20CC067A795E10F4B916908DCAA272EA240F1BB82871A2A3F4FFF0D028133F4220E0C829F592D93B2E52F04F4553E36DF9FC6A958959F91361ABD206476063AC1535C427384AACAA2B15873731A7681B29E4D7F2A525DE681E410E156BDBDAB57489DCDD56BD71";
const char RSA_E[]="010001";
const char RSA_D[]="3860EF24B4655C1B2163766DCEFE00798F63ED4D62F6A4CEE467288895277A713732DF18B5E7E09D0E244ECB397579504006CD09D6631FC52FE4479B569CDA780CF105F2FB93351C98A5D9C9565AFF15628366AEF930D88845B63469500E30947D3FA886CD03A14CF88DF4FCCA8C87C5EB219BE81E437D870170C45D43044F57739265C9A9D947901BEBEFA6D44A892F4BB2A5EFE0A436A0E2494B29C6BFDBA3D77DDBA9FA81011B2C1FD2105E7F50FED03D1F885125D5B31D6BBC5BFDDF07420CE6B8802C53AF2D539EC3522788B76B24C1398E2C1B4192BE22C80A67BD0A3F7095212C0DE907243DB4D0D227CE50C1F70FEDE87F95631BCDC1306CC371F205";
const char RSA_P[]="E3957AE9EABC8CD396EC1C485078C593382513ED0927EE00DCEFE68CA5232759E0E24DDA49DE51FE3DC2E1A3B84FD7130D6B22A4306E88002CCE95FE06AAF4CA05409EC371543C2264CB5169FE1A9BC361A5BF2E9F8EF8B828F8E357E54E146052684A5F2960E5D8589847189345260756714877B005B4FCE4B1952860AC66E3";
const char RSA_Q[]="CB5E31771866AE8826F77BFBE8AE2E6E75ABA68A8CFA163D220EAB3D81FDC9238A84557F429EDE7A0B543C4F5E9C8BE74B54D7EEBB148FDB5283A93A922FC268E5A012C33F52B1002BC42B8C596A9F58C0D2DB2BF13176905239F8318FDF7D61036A0908B9832E193CF336EB6507EEB35BDB94EF76E65325011024047812669B";
const char RSA_DP[]="A3EF4122CE9C13353739CD05AA31D4E03F49361940C72A8224A40A86B54DA542F0E3130172C45A7BB1317827DED46430AD31C73A4E48D05E8FE81FD3642A313A749E1FBED91BBC556A15AB0796AAC418F175DB495256428325C062C325C2209B61C10E118E54E63BF95577A11434733845E4443732EC697AE1A1A9B7F42B3BD1";
const char RSA_DQ[]="BEE0C1FCCE62521E68B4912277DA44AA58B7ABB10F710BBE8560CF4903E178106BCA9994C0AAEC96105C17DF4726180A17A2A2A9E7DDFCC816428D6BF419EF97152F916CC0DA94575CBCDB42F80A2355E2660660D01964F740B638460C8BEFDA46A217A8A0B6876618D70F0D11DEC824806B30F731DA2CDFE68787C6CA0C3B51";
const char RSA_QP[]="2EED8AEED167F30DDB5087AC15B305331B0B86EAB317216FCF6B834B5B85251D27CA2F75F6C16991DF554569250DA29A0F6973632D3EAA1B60D4871541CC1A9EEC9FE7E3E660856DB4C562C02281B1794C3F8994A72FC7923DE232CAEDAF999CAA5E694462B9F170B3EB0607D6D820F96C1CF3E67365DF2ED79AF269B83A1973";


//   do  a test or not 
#define   TEST_ECDSA_SIGNATURE    0
#define   ECDSA_SELF_SIGN		  0
#define   DEBUG_ECDSA_PRINT  	  0


#define TV_NAME(name) name " -- [" __FILE__ ":" STRINGIFY(__LINE__) "]"
#define  CHECK_RESULT(exp_res,chk) \
         do \
         { \
            if(chk != exp_res){ \
                printk("In function %s, line:%d, error_code:0x%04X \n", __func__,__LINE__,chk);\ 
            } \   	
         } while (0)
         
#define ECDSA_MAX_INPUT_SIZE (64)

typedef struct {
	const u32_t src_line_num; /**< Test vector source file line number. */
	const u32_t curve_type; /**< Curve type for test vector. */
	const int expected_sign_err_code; /**< Expected error code from ECDSA sign
									   operation. */
	const int expected_verify_err_code; /**< Expected result of following ECDSA
										 verify operation. */
	const char *p_test_vector_name; /**< Pointer to ECDSA test vector name. */
	char * p_input; /**< Pointer to ECDSA hash input in hex string format. */
	const char *
		p_qx; /**< Pointer to ECDSA public key X component in hex string
					   format. */
	const char *
		p_qy; /**< Pointer to ECDSA public key Y component in hex string
					   format. */
	const char *
		p_x; /**< Pointer to ECDSA private key component in hex string format. */
} test_vector_ecdsa_sign_t;

mbedtls_ctr_drbg_context ctr_drbg_ctx;
test_vector_ecdsa_sign_t test_case_ecdsa_data = {
	.curve_type = MBEDTLS_ECP_DP_SECP256R1,
	.expected_sign_err_code = 0,
	.expected_verify_err_code = 0,
	.p_test_vector_name = TV_NAME("secp256r1 valid SHA256 1"),
#if(TEST_ECDSA_SIGNATURE)	
	.p_input =
		"44acf6b7e36c1342c2c5897204fe09504e1e2efb1a900377dbc4e7a6a133ec56",
	.p_qx = "1ccbe91c075fc7f4f033bfa248db8fccd3565de94bbfb12f3c59ff46c271bf83",
	.p_qy = "ce4014c68811f9a21a1fdb2c0e6113e06db7ca93b7404e78dc7ccd5ca89a4ca9",
	.p_x = "519b423d715f8b581f4fa8ee59f4771a5b44c8130b4e3eacca54a56dda72b464"
#else
	.p_qx = "30a8f41d35ba3cfe39cc1effab498683796e53191abf1226c3637c801556bdf8",
	.p_qy = "7ffe428c9ad533d802b8487b319ffc2435a100536ac5fba87052354f52ba1713",
	.p_x = "aaf9d8f11ccd608c5be40f1950c55654480ccbbef4be8874b29ac11736017281"
#endif	
};

static test_vector_ecdsa_sign_t *p_test_vector_sign;
#if(TEST_ECDSA_SIGNATURE)
static uint8_t m_ecdsa_input_buf[ECDSA_MAX_INPUT_SIZE];
#else
static char* m_ecdsa_input_buf;
#endif
static size_t hash_len, initOKFlg=0;

static int entropy_func(void *ctx, unsigned char *buf, size_t len)
{
	return entropy_get_entropy(ctx, buf, len);
}

static int init_ctr_drbg(const unsigned char *p_optional_seed, size_t len)
{
	static const unsigned char ncs_seed[] = "ncs_drbg_seed";

	const unsigned char *p_seed;

	if (p_optional_seed == NULL) {
		p_seed = ncs_seed;
		len = sizeof(ncs_seed);
	} else {
		p_seed = p_optional_seed;
	}

        // v1.3.0-sdk-update
	//struct device *p_device = device_get_binding(CONFIG_ENTROPY_NAME);
        struct device *p_device =device_get_binding(DT_LABEL(DT_CHOSEN(zephyr_entropy)));
	if (p_device == NULL) {
		return -ENODEV;
	}

	mbedtls_ctr_drbg_init(&ctr_drbg_ctx);
        ctr_drbg_ctx.entropy_len = 144;
	return mbedtls_ctr_drbg_seed(&ctr_drbg_ctx, entropy_func, p_device,
				     p_seed, len);
}
#if(TEST_ECDSA_SIGNATURE)
__attribute__((noinline)) static void unhexify_ecdsa_sign(void)
{
	hash_len = hex2bin(p_test_vector_sign->p_input,
			strlen(p_test_vector_sign->p_input), m_ecdsa_input_buf,
			strlen(p_test_vector_sign->p_input));
}

void ecdsa_clear_buffers(void)
{
	memset(m_ecdsa_input_buf, 0x00, sizeof(m_ecdsa_input_buf));
}

void ecdsa_setup_sign(void)
{
	ecdsa_clear_buffers();	
	p_test_vector_sign = &test_case_ecdsa_data;
	unhexify_ecdsa_sign();
}
#endif
static void exec_test_case_ecdsa_sign(char *buf,int *len)
{
	int err_code = -1;
	unsigned int len_r,len_s;
	
	/* Prepare signer context. */
	mbedtls_ecdsa_context ctx_sign;
	mbedtls_ecdsa_init(&ctx_sign);

	err_code = mbedtls_ecp_group_load(&ctx_sign.grp,
					  p_test_vector_sign->curve_type);
	CHECK_RESULT(0,err_code);

	/* Get public key. */
	err_code = mbedtls_ecp_point_read_string(&ctx_sign.Q, 16,
						 p_test_vector_sign->p_qx,
						 p_test_vector_sign->p_qy);
	CHECK_RESULT(0, err_code);

	/* Get private key. */
	err_code = mbedtls_mpi_read_string(&ctx_sign.d, 16,
					   p_test_vector_sign->p_x);
	CHECK_RESULT(0, err_code);

	/* Verify keys. */
	err_code = mbedtls_ecp_check_pubkey(&ctx_sign.grp, &ctx_sign.Q);
	//LOG_DBG("Error code pubkey check: 0x%04X", err_code);
	CHECK_RESULT(0, err_code);

	err_code = mbedtls_ecp_check_privkey(&ctx_sign.grp, &ctx_sign.d);
	//LOG_DBG("Error code privkey check: 0x%04X", err_code);
	CHECK_RESULT(0, err_code);

	/* Prepare and generate the ECDSA signature. */
	/* Note: The contexts do not contain these (as is the case for e.g. Q), so simply share them here. */
	mbedtls_mpi r;
	mbedtls_mpi s;
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	//start_time_measurement();
   
	err_code = mbedtls_ecdsa_sign(&ctx_sign.grp, &r, &s, &ctx_sign.d,
				      m_ecdsa_input_buf, hash_len,
				      mbedtls_ctr_drbg_random, &ctr_drbg_ctx);    
	//stop_time_measurement();
	/* Verify sign. */
	CHECK_RESULT(p_test_vector_sign->expected_sign_err_code,
				 err_code);

#if  DEBUG_ECDSA_PRINT
	unsigned char buf_r[32],buf_s[32];	
	//memcpy(buf_r, r.p, r.n);

	//*len_r = r.n;	
	//memcpy(buf_s, s.p, s.n);
	//*len_s = s.n;
    len_r = mbedtls_mpi_size(&r);
    len_s  = mbedtls_mpi_size(&s);
    mbedtls_mpi_write_binary(&r, buf_r, len_r);
    mbedtls_mpi_write_binary(&s, buf_s, len_s);
	int i;
	char *ptmp;
	printk("r: %d \n",len_r);
	for(i =0, ptmp = (char*)buf_r; i < *len_r; i++)
	{
		printk("%x ", ptmp[i]);
	}	
	printk("\n");
	
	printk("s:%d \n",len_s);
	for(i =0, ptmp = (char*)buf_s; i < *len_s; i++)
	{
		printk("%x ", ptmp[i]);
	}	
	printk("\n");

#else
    len_r = mbedtls_mpi_size(&r);
    len_s  = mbedtls_mpi_size(&s);
    mbedtls_mpi_write_binary(&r, buf, len_r);
    mbedtls_mpi_write_binary(&s, buf+len_r, len_s);
	*len = len_r+len_s;
#endif

#if ECDSA_SELF_SIGN
	/* Prepare verification context. */
	mbedtls_ecdsa_context ctx_verify;
	mbedtls_ecdsa_init(&ctx_verify);

	/* Transfer public EC information. */
	err_code = mbedtls_ecp_group_copy(&ctx_verify.grp, &ctx_sign.grp);
	CHECK_RESULT(0, err_code);

	/* Transfer public key. */
	err_code = mbedtls_ecp_copy(&ctx_verify.Q, &ctx_sign.Q);
	CHECK_RESULT(0, err_code);

	/* Verify the generated ECDSA signature by running the ECDSA verify. */
	err_code = mbedtls_ecdsa_verify(&ctx_verify.grp, m_ecdsa_input_buf,
					hash_len, &ctx_verify.Q, &r, &s);

	CHECK_RESULT(p_test_vector_sign->expected_verify_err_code,
				 err_code);
#endif
	/* Free resources. */
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_ecdsa_free(&ctx_sign);
#if ECDSA_SELF_SIGN	
	mbedtls_ecdsa_free(&ctx_verify);
#endif	
}

int initECDSA_sep256r(void)
{
    if (init_ctr_drbg(NULL, 0) != 0) {
		initOKFlg = 0;
        printk("Bad ctr drbg init!");		
		return  1;
    }   
	initOKFlg = 1;
	return 0;
}

int doESDA_sep256r_Sign(char *inbuf, uint32_t len, char *buf, int* sinlen)
{
	char shaBuf[32];
	int err_code = -1, i;
	if(!initOKFlg)
		return -1;
	p_test_vector_sign = &test_case_ecdsa_data;
//	char * shaBuf = (char*)mbedtls_calloc(1, len);
//	if(shaBuf == NULL){
		//printk("doESDA_sep256r_Sign  alloc fail!\n");
		//return -1;
	//}
	err_code = mbedtls_sha256_ret(inbuf, len,  shaBuf,false);
	CHECK_RESULT(0, err_code);
	p_test_vector_sign->p_input = shaBuf;
	m_ecdsa_input_buf = shaBuf;
	hash_len = 32;
#if DEBUG_ECDSA_PRINT	
	printk("SHA256: \n");
	for(i = 0; i< 32 ; i++)
	{
		printk("%x ", shaBuf[i]);
	}
	printk("\n");
#endif
	exec_test_case_ecdsa_sign(buf, sinlen);
	
	return err_code;
}

void hex2str(char* buf_hex, int len, char *str)
{
	int i,j;
	for(i =0,j=0; i< len; i++)
	{
		str[j++] = (buf_hex[i]&0x0F) > 9 ? ((buf_hex[i]&0x0F)-10 +'A'):((buf_hex[i]&0x0F)+'0');
		str[j++] = (buf_hex[i]&0xF0) > 0x90 ? (((buf_hex[i]&0xF0)>>4)-10 + 'A'):(((buf_hex[i]&0xF0)>>4)+'0');		
	}
	str[j] = 0;	
}


int RSA_decrypt(char *input, int len, char *out, int out_len)
{   
    int ret = 1;
    int exit_code = 1;
    size_t i;
    mbedtls_rsa_context rsa;
    mbedtls_mpi N, P, Q, D, E, DP, DQ, QP;
    mbedtls_ctr_drbg_context ctr_drbg;
    memset(out, 0, out_len );
    IOTEX_CRYPTO_DEBUG_OUTPUT( "\n  . Seeding the random number generator..." ); 

    mbedtls_rsa_init( &rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256 );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    mbedtls_mpi_init( &N ); mbedtls_mpi_init( &P ); mbedtls_mpi_init( &Q );
    mbedtls_mpi_init( &D ); mbedtls_mpi_init( &E ); mbedtls_mpi_init( &DP );
    mbedtls_mpi_init( &DQ ); mbedtls_mpi_init( &QP );

    IOTEX_CRYPTO_DEBUG_OUTPUT( "\n  . Reading private key from rsa_priv.txt" );

    if( ( ret = mbedtls_mpi_read_string( &N , 16, RSA_N ) )  != 0 ||
        ( ret = mbedtls_mpi_read_string( &E , 16, RSA_E ) )  != 0 ||
        ( ret = mbedtls_mpi_read_string( &D , 16, RSA_D ) )  != 0 ||
        ( ret = mbedtls_mpi_read_string( &P , 16, RSA_P ) )  != 0 ||
        ( ret = mbedtls_mpi_read_string( &Q , 16, RSA_Q ) )  != 0 ||
        ( ret = mbedtls_mpi_read_string( &DP , 16, RSA_DP ) ) != 0 ||
        ( ret = mbedtls_mpi_read_string( &DQ , 16, RSA_DQ ) ) != 0 ||
        ( ret = mbedtls_mpi_read_string( &QP , 16, RSA_QP ) ) != 0 )
    {
        printk( " failed\n  ! mbedtls_mpi_read_file returned %d\n\n",
                        ret );
        goto exit;
    }

    if( ( ret = mbedtls_rsa_import( &rsa, &N, &P, &Q, &D, &E ) ) != 0 )
    {
        printk( " failed\n  ! mbedtls_rsa_import returned %d\n\n",
                        ret );
        goto exit;
    }

    if( ( ret = mbedtls_rsa_complete( &rsa ) ) != 0 )
    {
        printk( " failed\n  ! mbedtls_rsa_complete returned %d\n\n",
                        ret );
        goto exit;
    }

	i = len;
    if( i != rsa.len )
    {
        printk( "\n  ! Invalid RSA signature format\n\n" );
        goto exit;
    }

    /*
     * Decrypt the encrypted RSA data and print the result.
     */
    IOTEX_CRYPTO_DEBUG_OUTPUT( "\n  . Decrypting the encrypted data" );

    ret = mbedtls_rsa_pkcs1_decrypt( &rsa, mbedtls_ctr_drbg_random,
                                            /*&ctr_drbg*/&ctr_drbg_ctx, MBEDTLS_RSA_PRIVATE, &i,
                                            input, out, out_len );
    if( ret != 0 )
    {
        printk( " failed\n  ! mbedtls_rsa_pkcs1_decrypt returned %d\n\n",
                        ret );
        goto exit;
    }

    IOTEX_CRYPTO_DEBUG_OUTPUT( "\n  . OK\n\n" );

    IOTEX_CRYPTO_DEBUG_OUTPUT( "The decrypted result is: '%s'\n\n", out );

    exit_code = 0;

exit:
    mbedtls_rsa_free( &rsa );
    mbedtls_mpi_free( &N ); mbedtls_mpi_free( &P ); mbedtls_mpi_free( &Q );
    mbedtls_mpi_free( &D ); mbedtls_mpi_free( &E ); mbedtls_mpi_free( &DP );
    mbedtls_mpi_free( &DQ ); mbedtls_mpi_free( &QP );

    return( exit_code );	
}

const char RSA_TEST_Str[]="Hello World !";
const char RSA_encryptedStr[]=
#if 1
{
0x00,0xc2,0xb6,0x5d,0xb6,0xdc,0xfc,0x0a,0x61,0xac,0xcf,0x92,0x62,0x14,0x55,0x9c,
0xed,0x42,0x38,0x4a,0x76,0x32,0x08,0xbc,0x4b,0x54,0xd0,0x26,0x21,0xbd,0x80,0x7e,
0x24,0xc5,0x0f,0x8d,0xa3,0x53,0x6c,0x68,0x79,0xf5,0x46,0xbd,0x00,0xd0,0x59,0xca,
0x49,0x58,0x61,0xd7,0xf0,0xa4,0x00,0x4d,0x84,0x2c,0x5a,0x6d,0xac,0x30,0x39,0x72,
0x08,0xe8,0xf9,0x1d,0x12,0x51,0x42,0xa6,0x52,0x23,0x11,0x5a,0x32,0x6f,0x42,0x58,
0x37,0x21,0xed,0xef,0x61,0xd8,0x16,0x06,0x52,0x18,0x43,0x03,0xdd,0x0c,0xff,0xd3,
0xc6,0x05,0xa0,0xfe,0x28,0x67,0xc6,0xd9,0xde,0xc5,0xd9,0x09,0xee,0x7a,0xfc,0x5b,
0x07,0x69,0x4b,0x1f,0x61,0x52,0x5f,0xf0,0xf6,0x43,0x6b,0x6a,0x3c,0xfe,0x58,0x2e,
0xe4,0xf1,0xaa,0xd5,0x2b,0xc4,0xab,0x4f,0x18,0xdc,0x31,0xe3,0x83,0x63,0x25,0x25,
0x1b,0xd5,0x96,0xb2,0xcc,0x9a,0x97,0x04,0x77,0x02,0x0e,0x4e,0xd2,0x48,0xf7,0x26,
0x0b,0xaa,0xa4,0x97,0xe3,0xc3,0x26,0xe7,0x0e,0x0b,0x13,0xe5,0xf0,0x3c,0x40,0xff,
0xa4,0xf1,0x47,0xa2,0x47,0x22,0x77,0xf8,0x19,0x01,0x3b,0x7f,0x41,0xd7,0xdc,0xd6,
0xf1,0x30,0x72,0xd2,0x3c,0x11,0x1b,0xac,0xa2,0xa0,0x10,0x9f,0x84,0x35,0x39,0xeb,
0x48,0xca,0x1d,0xdc,0x1a,0x15,0x32,0x05,0x24,0x0e,0x91,0xd3,0x24,0xec,0x4c,0x1a,
0x8d,0x95,0x96,0x10,0xf8,0x83,0xc2,0x8f,0x84,0x09,0xe4,0x73,0xf6,0x51,0x2c,0xe8,
0xb1,0xa2,0xea,0xab,0x9b,0x63,0x15,0x83,0x1f,0x97,0x5e,0xf1,0xbe,0x62,0x79,0xe0
};
#else
{
0x47,0x7c,0x2c,0x90,0x31,0xf6,0x07,0x51,0xfc,0xb2,0xc2,0xaa,0x48,0xa2,0x24,0xc7,
0x07,0x4f,0xc9,0x52,0x12,0xf2,0x4a,0x27,0xb5,0x51,0x4e,0xd5,0x1f,0x5f,0x8e,0x50,
0x15,0x4e,0x32,0xaf,0xe8,0x0a,0x80,0x8a,0x3c,0x8c,0x3c,0xf2,0xfb,0xc8,0xbe,0x59,
0x4d,0x15,0x5b,0x08,0x14,0xbd,0xb0,0xe8,0xaf,0xbf,0x0a,0x51,0xb8,0x9f,0xc0,0x6b,
0xd9,0xf9,0x6d,0xec,0xd2,0x5d,0x47,0x8a,0x3c,0xcb,0x50,0xbe,0xb4,0x70,0xac,0xdf,
0xce,0x41,0x2c,0xba,0xe8,0x26,0x3b,0x7c,0xef,0x7f,0xcd,0x60,0xf0,0x9c,0x09,0x4c,
0x7d,0xfa,0x0a,0xee,0xd6,0xf2,0xf1,0xcf,0x7c,0xd6,0xdc,0x81,0x28,0xf0,0x66,0x99,
0x18,0xaa,0xe7,0xff,0xeb,0x20,0xad,0x84,0x5e,0x09,0x4e,0x86,0x07,0x24,0xa6,0x45,
0xcb,0x6e,0x9e,0xb5,0x7f,0x93,0x60,0xac,0x47,0x7f,0xb1,0xfc,0x00,0xeb,0x58,0x06,
0x60,0xd0,0x10,0xe2,0xfe,0xa8,0xd2,0xac,0x67,0x1e,0x63,0x45,0x5e,0x81,0x35,0xe9,
0xe1,0xd1,0xca,0xf0,0xa6,0x37,0xf5,0x89,0x81,0x18,0x78,0x9d,0x7e,0x8b,0x1f,0xc4,
0xe2,0xa4,0x13,0x0b,0x3a,0x56,0xa9,0xa2,0x2a,0x7c,0xa8,0x2e,0xe4,0x8b,0x43,0xed,
0x1f,0x8d,0x3a,0xf1,0xfa,0xb8,0xdd,0xae,0x71,0x1d,0x76,0x0d,0x1a,0x3c,0xa5,0xb3,
0xcd,0x66,0x9a,0x21,0x24,0xf9,0xb6,0x21,0xe1,0xcb,0x5e,0x69,0x3f,0xee,0x05,0x9a,
0x65,0x84,0xdc,0xc7,0x26,0xf0,0x03,0xf6,0xbd,0x5c,0xc7,0x76,0x00,0x4e,0x49,0xf5,
0x08,0xdd,0x75,0x32,0x11,0x5c,0xe7,0x83,0xe3,0x44,0xa3,0x08,0xf0,0xdb,0x7c,0xb1
};
#endif


//generate an RSA key pair
int RSA_gen_key(void)
{
    int ret = 1;
    int exit_code = 1;
    mbedtls_rsa_context rsa;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_mpi N, P, Q, D, E, DP, DQ, QP;

    const char *pers = "iotex_rsa_genkey";
	char s[ MBEDTLS_MPI_RW_BUFFER_SIZE ];
	size_t n;
	printk("start\n");

    mbedtls_ctr_drbg_init( &ctr_drbg );
    mbedtls_rsa_init( &rsa, MBEDTLS_RSA_PKCS_V21, 0 );
    mbedtls_mpi_init( &N ); mbedtls_mpi_init( &P ); mbedtls_mpi_init( &Q );
    mbedtls_mpi_init( &D ); mbedtls_mpi_init( &E ); mbedtls_mpi_init( &DP );
    mbedtls_mpi_init( &DQ ); mbedtls_mpi_init( &QP );

    printk( "\n  . Seeding the random number generator..." );    


    // v1.3.0-sdk-update
	//struct device *p_device = device_get_binding(CONFIG_ENTROPY_NAME);
    struct device *p_device =device_get_binding(DT_LABEL(DT_CHOSEN(zephyr_entropy)));
	if (p_device == NULL) {
		return -ENODEV;
	}

    //mbedtls_entropy_init( &entropy );
	ctr_drbg.entropy_len = 144;
    if( ( ret = mbedtls_ctr_drbg_seed( &ctr_drbg, /*mbedtls_entropy_func*/ entropy_func, /*&entropy*/p_device,
                               (const unsigned char *) pers,
                               strlen( pers ) ) ) != 0 )
    {
        printk( " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret );
        goto exit;
    }

    printk( " ok\n  . Generating the RSA key [ %d-bit ]...", KEY_SIZE ); 

    if( ( ret = mbedtls_rsa_gen_key( &rsa, mbedtls_ctr_drbg_random, &ctr_drbg, KEY_SIZE,
                                     EXPONENT ) ) != 0 )
    {
        printk( " failed\n  ! mbedtls_rsa_gen_key returned %d\n\n", ret );
        goto exit;
    }

    printk( " ok\n  . Exporting the public  key in rsa_pub.txt.... \n\n" );    

    if( ( ret = mbedtls_rsa_export    ( &rsa, &N, &P, &Q, &D, &E ) ) != 0 ||
        ( ret = mbedtls_rsa_export_crt( &rsa, &DP, &DQ, &QP ) )      != 0 )
    {
        printk( " failed\n  ! could not export RSA parameters\n\n" );
        goto exit;
    }

	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&N, 16, s, sizeof( s ) - 2, &n );
	printk("N = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&E, 16, s, sizeof( s ) - 2, &n );
	printk("E = %s\n\n",s);	

    printk( " ok\n  . Exporting the private key in rsa_priv.txt...\n\n" );    

	memset( s, 0, sizeof( s ) );	
	mbedtls_mpi_write_string(&N, 16, s, sizeof( s ) - 2, &n );
	printk("N = %s\n\n",s);
	memset( s, 0, sizeof( s ) );	
	mbedtls_mpi_write_string(&E, 16, s, sizeof( s ) - 2, &n );
	printk("E = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&D, 16, s, sizeof( s ) - 2, &n );
	printk("D = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&P, 16, s, sizeof( s ) - 2, &n );
	printk("P = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&Q, 16, s, sizeof( s ) - 2, &n );
	printk("Q = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&DP, 16, s, sizeof( s ) - 2, &n );
	printk("DP = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&DQ, 16, s, sizeof( s ) - 2, &n );
	printk("DQ = %s\n\n",s);
	memset( s, 0, sizeof( s ) );
	mbedtls_mpi_write_string(&QP, 16, s, sizeof( s ) - 2, &n );
	printk("QP = %s\n\n",s);

    printk( " ok\n\n" );

    exit_code = 0;

exit:
    mbedtls_mpi_free( &N ); mbedtls_mpi_free( &P ); mbedtls_mpi_free( &Q );
    mbedtls_mpi_free( &D ); mbedtls_mpi_free( &E ); mbedtls_mpi_free( &DP );
    mbedtls_mpi_free( &DQ ); mbedtls_mpi_free( &QP );
    mbedtls_rsa_free( &rsa );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy );

    return( exit_code );	
}

int RSA_encrypt(void)
{
    int ret = 1;
    int exit_code = 1;
    size_t i;
    mbedtls_rsa_context rsa;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    unsigned char input[1024];
    unsigned char buf[512];
    const char *pers = "iotex_rsa_genkey";
    mbedtls_mpi N, E;    

	mbedtls_ctr_drbg_init( &ctr_drbg );   
    mbedtls_mpi_init( &N ); mbedtls_mpi_init( &E );
    mbedtls_rsa_init( &rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256 );
    struct device *p_device =device_get_binding(DT_LABEL(DT_CHOSEN(zephyr_entropy)));
	if (p_device == NULL) {
		return -ENODEV;
	}
	printk( "\n  . Seeding the random number generator..." );  
	ctr_drbg.entropy_len = 144;
    ret = mbedtls_ctr_drbg_seed( &ctr_drbg, entropy_func,
                                 p_device, (const unsigned char *) pers,
                                 strlen( pers ) );
    if( ret != 0 )
    {
        printk( " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n",
                        ret );
        goto exit;
    }

    printk( "\n  . Reading public key from rsa_pub.txt" );    

	if( ( ret = mbedtls_mpi_read_string(&N, 16, RSA_N )  ) != 0 ||
	    ( ret = mbedtls_mpi_read_string( &E, 16, RSA_E ) ) != 0 )
    {
       printk( " failed\n  ! mbedtls_mpi_read_file returned %d\n\n",ret );    
        goto exit;
    }		

    if( ( ret = mbedtls_rsa_import( &rsa, &N, NULL, NULL, NULL, &E ) ) != 0 )
    {
        printk( " failed\n  ! mbedtls_rsa_import returned %d\n\n",
                        ret );
        goto exit;
    }

    memcpy( input, RSA_TEST_Str, strlen( RSA_TEST_Str ) );

    /*
     * Calculate the RSA encryption of the hash.
     */
    printk( "\n  . Generating the RSA encrypted value \n\n" );  

    ret = mbedtls_rsa_pkcs1_encrypt( &rsa, mbedtls_ctr_drbg_random,
                                     &ctr_drbg, MBEDTLS_RSA_PUBLIC,
                                     strlen( RSA_TEST_Str ), input, buf );									 
    if( ret != 0 )
    {
        printk( " failed\n  ! mbedtls_rsa_pkcs1_encrypt returned %d\n\n",
                        ret );
        goto exit;
    }

    for( i = 0; i < rsa.len; i++ )
        printk( "0x%02X%s", buf[i],
                 ( i + 1 ) % 16 == 0 ? ",\r\n" : "," );
        //printk( "%02X", buf[i]);        
    

    printk( "\n  . Done (created \"%s\")\n\n", "result-enc.txt" );

    exit_code = 0;

exit:
    mbedtls_mpi_free( &N ); mbedtls_mpi_free( &E );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy );
    mbedtls_rsa_free( &rsa );
    printk( "  + Press Enter to exit this program.\n" );    
    printk( "  + Press Enter to exit this program.\n" );    

    return( exit_code );
}
