#include <stdio.h>
#include <zephyr.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ctr_drbg.h>
#include "ecdsa.h"
#include <drivers/entropy.h>
#include <sys/util.h>
#include <toolchain/common.h>
#include "ecdsa.h"

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

