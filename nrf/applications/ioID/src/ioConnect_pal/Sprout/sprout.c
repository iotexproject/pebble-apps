#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/tls_credentials.h>

#include "include/jose/jose.h"
#include "include/dids/dids.h"
#include "include/utils/baseX/base64.h"
#include "include/utils/convert/convert.h"

#include "sprout.h"
#include "ecdsa.h"
#include "config.h"

#define IOTEX_HTTP_USER_DATA_STRING_QUERY                  "Query"     
#define IOTEX_HTTP_USER_DATA_STRING_UPLOAD_CONFIG          "Uplaod_Config"     
#define IOTEX_HTTP_USER_DATA_STRING_SEND_MESSAGE           "Send_Message"
#define IOTEX_HTTP_USER_DATA_STRING_REQUEST_PUBKEY         "Request_PubKey"

LOG_MODULE_REGISTER(sprout, CONFIG_ASSET_TRACKER_LOG_LEVEL);

static char _messageID[IOTEX_PAL_SPROUT_MESSAGE_ID_MAX_SIZE]        = {0};
static char _didcommToken[IOTEX_PAL_SPROUT_HTTP_HEADER_MAX_SIZE]    = {0};
static char _replyData[IOTEX_PAL_SPROUT_HTTP_REPLY_BUF_MAX_SIZE]    = {0};
#ifdef IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
static char _serverKAKID[IOTEX_PAL_SPROUT_SERVER_KA_KID_MAX_SIZE]   = {0};
#endif

static psa_key_id_t server_pubkey_id = 0;
static int    _sock = -1;
static int    _err_times = 0;

static u_int32_t timestamp_utc  = 0;
static u_int32_t timestamp_base = 0;

static char *_deviceDID = NULL;
static char *_deviceKAKID = NULL;
static char *_client_id_serialize = NULL;
static char *_sprout_query = NULL;

static char *_ota_url = NULL;
static char *_ota_ver = NULL;

static struct addrinfo *res = NULL;

typedef struct _pal_sprout_ctx {

    uint8_t type;
    uint8_t *_replyData;
    uint8_t err_times;
    uint8_t isRegister;

    uint32_t status;

    bool hasFinish;
    struct k_work work;

    struct k_mutex _sprout_mutex;

} _pa_sprout_ctx_t;

static struct _pal_sprout_ctx _sprout_ctx;

const char *_headers[] = {
    _didcommToken,
    NULL
};

uint32_t iotex_pal_sprout_sensor_data_timestamp_get(void)
{
    return 0 == timestamp_utc ? 0 : ((u_int32_t)(k_uptime_get() / 1000) - timestamp_base) + timestamp_utc;
}

char * iotex_pal_sprout_ota_url_get(void)
{
    return _ota_url;
}

char * iotex_pal_sprout_ota_ver_get(void)
{
    return _ota_ver;
}

static void _pal_sprout_ctx_init(void)
{
    if (_sprout_ctx._replyData) {
        free(_sprout_ctx._replyData);
        _sprout_ctx._replyData = NULL;
    } 

    memset(&_sprout_ctx, 0, sizeof(_sprout_ctx));  
}

static void _pal_sprout_ctx_deinit(void)
{
    if (_sprout_ctx._replyData) {
        free(_sprout_ctx._replyData);
        _sprout_ctx._replyData = NULL;
    }

    _sprout_ctx.type      = IOTEX_PAL_SPROUT_CTX_TYPE_INIT;
    _sprout_ctx.err_times = 0;
    _sprout_ctx.hasFinish = false;
}

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
static int _pal_sprout_token_handle(char *cipher_token)
{
    if (NULL == cipher_token)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    if (NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    char *token = iotex_jwe_decrypt(cipher_token, Ecdh1puA256kw, A256cbcHs512, NULL, NULL, (char *)_deviceKAKID);
    if (token) {
        memset(_didcommToken + strlen(IOTEX_SPROUT_HTTP_HEADER_HEAD), 0, IOTEX_PAL_SPROUT_HTTP_HEADER_MAX_SIZE - strlen(IOTEX_SPROUT_HTTP_HEADER_HEAD));
        memcpy(_didcommToken + strlen(IOTEX_SPROUT_HTTP_HEADER_HEAD), token, strlen(token));
        memcpy(_didcommToken + strlen(IOTEX_SPROUT_HTTP_HEADER_HEAD) + strlen(token), IOTEX_SPROUT_HTTP_HEADER_END, strlen(IOTEX_SPROUT_HTTP_HEADER_END));

        LOG_INF("Token : %s", _didcommToken + + strlen(IOTEX_SPROUT_HTTP_HEADER_HEAD));

        free(token);

        _err_times = 0;

        _sprout_ctx.status |= IOTEX_PAL_SPROUT_STATUS_REQUEST_TOKEN;

    } else {
        LOG_ERR("Failed to decrypt token");
        return IOTEX_SPROUT_ERR_ENCRYPT_FAIL;
    }

    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
static int _pal_sprout_diddoc_handle(char *diddoc)
{
    DIDDoc *diddoc_parse = NULL;

    if (NULL == diddoc)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    diddoc_parse = iotex_diddoc_parse(diddoc);
    if (NULL == diddoc_parse) {

        _sprout_ctx.err_times++;

        LOG_ERR("Failed to Parse the DIDDoc \n");          
        return;
    }

    unsigned int vm_num = iotex_diddoc_verification_method_get_num(diddoc_parse, VM_PURPOSE_KEY_AGREEMENT);
    if (0 == vm_num) {
       
       _sprout_ctx.err_times++;

        LOG_ERR("Not Find Verification Method\n");
        goto diddoc_destroy;
    }

    VerificationMethod_Info *vm_info = iotex_diddoc_verification_method_get(diddoc_parse, VM_PURPOSE_KEY_AGREEMENT, vm_num - 1);             
    if (NULL == vm_info) {
        _err_times++;

        LOG_ERR("Not Find Key Agreement Method in Verification Methods\n");
        goto diddoc_destroy;
    }

    if (vm_info->pubkey_type == VERIFICATION_METHOD_PUBLIC_KEY_TYPE_JWK) {
        iotex_registry_item_register(vm_info->id, vm_info->pk_u.jwk); 
        memset(_serverKAKID, 0, sizeof(_serverKAKID));
        memcpy(_serverKAKID, vm_info->id, strlen(vm_info->id));

        LOG_INF("KA_KID from Server : %s", _serverKAKID);

        _sprout_ctx.status |= IOTEX_PAL_SPROUT_STATUS_GET_DIDDOC_OF_SERVER;
    } 

diddoc_destroy:
    if (diddoc_parse)
        iotex_diddoc_destroy(diddoc_parse); 

    _err_times = 0;

    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif 

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
static int _pal_sprout_send_messge_handle(char *resp)
{
    if (NULL == resp)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    if (NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    char *plaintext = iotex_jwe_decrypt(resp, Ecdh1puA256kw, A256cbcHs512, NULL, NULL, (char *)_deviceKAKID);
    if (NULL == plaintext) {
         LOG_ERR("Failed to Decrypt Message Response");  
         return IOTEX_SPROUT_ERR_ENCRYPT_FAIL;
    }
    
    LOG_INF("Receive Message : %s\n", plaintext);

    cJSON *message_id_root = cJSON_Parse(plaintext);
    if (NULL == message_id_root) {
        
        _err_times++;
        LOG_ERR("Response Message DataFormat Error");
        return IOTEX_SPROUT_ERR_DATA_FORMAT;
    }

    cJSON *message_id_item = cJSON_GetObjectItem(message_id_root, "_messageID");
    if (NULL == message_id_item) {
        
        _err_times++;
        LOG_ERR("Response Message DataFormat Error : No <_messageID> item");
        goto exit;
    }

    memset(_messageID, 0, IOTEX_PAL_SPROUT_MESSAGE_ID_MAX_SIZE);
    memcpy(_messageID, message_id_item->valuestring, strlen(message_id_item->valuestring));

    LOG_INF("Got Message ID : %s", _messageID);

    free(plaintext);

    _err_times = 0;

exit:
    
    cJSON_Delete(message_id_root);  

    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
static int _pal_sprout_query_handle(char *resp)
{
    if (NULL == resp)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    if (NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    char *msg_state = iotex_jwe_decrypt(resp, Ecdh1puA256kw, A256cbcHs512, NULL, NULL, _deviceKAKID);
    if (NULL == msg_state) {
        LOG_ERR("Failed to decrypt message status\n");  
        return IOTEX_SPROUT_ERR_ENCRYPT_FAIL;
    }

    LOG_INF("Receive Message State: %s\n", msg_state);
    
    _err_times = 0;
    
    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif

static int _pal_sprout_device_query_handle(char *resp)
{
    int ret = IOTEX_SPROUT_ERR_SUCCESS;
    uint8_t signature[65] = {0};

    if (NULL == resp)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    LOG_INF("Receive Query State: %s", resp);
    
    _err_times = 0;

    cJSON *device_status =  cJSON_Parse(resp); 
    if (NULL == device_status)
        return IOTEX_SPROUT_ERR_DATA_FORMAT;

    cJSON *signature_item = cJSON_GetObjectItem(device_status, "signature");
    if (NULL == signature_item || !cJSON_IsString(signature_item)) {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        goto exit;
    } 

    if (132 != strlen(signature_item->valuestring)) {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        LOG_ERR("Quary Signature Size Err : %d", strlen(signature_item->valuestring));
        goto exit;
    }

    iotex_utils_convert_str_to_hex(signature_item->valuestring + 2, signature);

    char *pos = strstr(resp, "\"signature\"");
    if (pos) {
        int index = pos - resp;

        resp[index - 1] = '}';
        resp[index]     = 0;

        LOG_INF("Raw data : %s", resp);
    } else {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        LOG_ERR("Failed to find `Signature` in the strng");
        goto exit;
    }    

    psa_status_t status = psa_verify_message(server_pubkey_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), resp, strlen(resp), signature, 64);
    if (PSA_SUCCESS != status) {
        ret = IOTEX_SPROUT_ERR_VERIFY_FAIL;
        LOG_ERR("Failed to Quary Signature Verigy : %d", status);
        goto exit;
    }

    cJSON *timestamp_item = cJSON_GetObjectItem(device_status, "timestamp");
    if (NULL == timestamp_item || !cJSON_IsNumber(timestamp_item)) {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        goto exit;
    }  

    timestamp_utc = timestamp_item->valueint;
    LOG_INF("Receive Timestamp UTC: %d", timestamp_utc); 

    timestamp_base = (u_int32_t)(k_uptime_get() / 1000);
    LOG_INF("Receive Timestamp Boot: %d", timestamp_base); 

    _sprout_ctx.status |= IOTEX_PAL_SPROUT_STATUS_REQUEST_TIMESTAMP;    
    
    cJSON *ota_url_item = cJSON_GetObjectItem(device_status, "uri");
    if (NULL == ota_url_item || !cJSON_IsString(ota_url_item)) {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        goto exit;
    }  

    if (_ota_url) {
        free (_ota_url);
    }

    _ota_url = calloc(strlen(ota_url_item->valuestring) + 1, sizeof(char));
    if (NULL == _ota_url) {
        ret = IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;
        goto exit;
    }

    strcpy(_ota_url, ota_url_item->valuestring); 

    LOG_INF("Receive OTA URL : %s", _ota_url); 

    cJSON *ota_ver_item = cJSON_GetObjectItem(device_status, "version");
    if (NULL == ota_ver_item || !cJSON_IsString(ota_ver_item)) {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        goto exit;
    }  

    if (_ota_ver) {
        free (_ota_ver);
    }

    _ota_ver = calloc(strlen(ota_ver_item->valuestring) + 1, sizeof(char));
    if (NULL == _ota_ver) {
        ret = IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;
        goto exit;
    }

    strcpy(_ota_ver, ota_ver_item->valuestring); 

    LOG_INF("Receive OTA Ver : %s", _ota_ver); 

exit:
    if (device_status) {
        cJSON_Delete(device_status);    
    }

    return ret;
}

static int _pal_sprout_device_request_pubkey_handle(char *resp)
{
    int ret = IOTEX_SPROUT_ERR_SUCCESS;
    uint8_t pub[65] = {0};

    if (NULL == resp)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    LOG_INF("Receive Request Info: %s", resp);
    
    cJSON *server_pubkey_root =  cJSON_Parse(resp); 
    if (NULL == server_pubkey_root) {
        LOG_ERR("Cannot Parser Server PubKey Json struct");
        return IOTEX_SPROUT_ERR_DATA_FORMAT;
    }

    cJSON *public_key_item = cJSON_GetObjectItem(server_pubkey_root, "publicKey");
    if (NULL == public_key_item || !cJSON_IsString(public_key_item)) {
        LOG_ERR("Cannot Parser Server PubKey");
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        goto exit;
    }  

    if (132 != strlen(public_key_item->valuestring)) {
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        LOG_ERR("Server PubKey Size Err : %d", strlen(public_key_item->valuestring));
        goto exit;
    }

    iotex_utils_convert_str_to_hex(public_key_item->valuestring + 2, pub);
    if (pub[0] != 0x04) {
        LOG_ERR("Server PubKey Format Err");
        ret = IOTEX_SPROUT_ERR_DATA_FORMAT;
        goto exit;    
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_K1));
    psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_bits(&attributes, 256);     

    psa_status_t status = psa_import_key( &attributes, pub, sizeof(pub), &server_pubkey_id );
    if( status != PSA_SUCCESS ) {   
        LOG_ERR("Import Server PubKey Err : %d", status);
        ret = IOTEX_SPROUT_ERR_IMPORT_PUBKEY_FAIL;
        goto exit;
    }

    LOG_INF("Receive Server PubKey [%x]: %s", server_pubkey_id, public_key_item->valuestring); 

    _sprout_ctx.status |= IOTEX_PAL_SPROUT_STATUS_REQUEST_PUBKEY;

exit:
    if (server_pubkey_root) {
        cJSON_Delete(server_pubkey_root);    
    }

    return ret;
}

void _pal_sprout_http_response_parse(struct k_work *item)
{
    if (NULL == _sprout_ctx._replyData)
        goto exit;

    switch (_sprout_ctx.type) {
        case IOTEX_PAL_SPROUT_CTX_TYPE_SERVER_PUBKEY:

            _pal_sprout_device_request_pubkey_handle(_sprout_ctx._replyData);

            break;        
#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM        
        case IOTEX_PAL_SPROUT_CTX_TYPE_SERVER_DIDDOC:
            
            _pal_sprout_diddoc_handle(_sprout_ctx._replyData);
        
            break;
        case IOTEX_PAL_SPROUT_CTX_TYPE_REQUEST_TOKEN:
            
             _pal_sprout_token_handle(_sprout_ctx._replyData);

            break;         
        case IOTEX_PAL_SPROUT_CTX_TYPE_SEND_MESSAGE:
            
            _pal_sprout_send_messge_handle(_sprout_ctx._replyData);

            break;
        case IOTEX_PAL_SPROUT_CTX_TYPE_QUERY_STATUS:

            _pal_sprout_query_handle(_sprout_ctx._replyData);

            break;
#endif            
        case IOTEX_PAL_SPROUT_CTX_TYPE_DEVICE_QUERY:

            _pal_sprout_device_query_handle(_sprout_ctx._replyData);

            break;                                
        default:
            break;
    }

exit:
    _pal_sprout_ctx_deinit();

    return;
}

static int _pal_sprout_http_server_connect(void)
{
	char peer_addr[INET_ADDRSTRLEN] = {0};
	
    // struct addrinfo hints, *res;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));

    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_socktype = SOCK_STREAM;

    if (res)
        goto setup;

    LOG_INF("Looking up %s", IOTEX_SPROUT_HTTP_HOST);
	int err = getaddrinfo(IOTEX_SPROUT_HTTP_HOST, IOTEX_SPROUT_HTTP_PORT_STRING, &hints, &res);
	if (err) {
		LOG_ERR("getaddrinfo() failed, err %d\n", errno);
        err = errno;
        goto exit; 
	}

	inet_ntop(res->ai_family, &((struct sockaddr_in *)(res->ai_addr))->sin_addr, peer_addr, INET_ADDRSTRLEN);
	LOG_INF("Resolved %s (%s) protocol %d\n", peer_addr, net_family2str(res->ai_family), res->ai_protocol);

setup:
    _sock = socket(res->ai_family, SOCK_STREAM, res->ai_protocol);
	if (_sock < 0)  {
		LOG_ERR("Failed to create HTTP socket (%d)", -errno);
        err = errno;
        goto exit_1;
    }

	err = connect(_sock, res->ai_addr, res->ai_addrlen);
	if (err) {
		LOG_ERR("connect() failed, err: %d\n", -errno);
        err = errno;
		goto exit_2;
	}

    LOG_INF("Connected to %s:%d\n", IOTEX_SPROUT_HTTP_HOST, ntohs(((struct sockaddr_in *)(res->ai_addr))->sin_port));    

    return 0;
	
exit_2:    
	close(_sock);
    _sock = -1;
exit_1:
    freeaddrinfo(res);
    res = NULL;
exit:
	return err;    
}

int iotex_pal_sprout_http_server_connect(void)
{
    return _pal_sprout_http_server_connect();
}

static void _pal_sprout_http_server_disconnect(void)
{
    LOG_INF("Disconnected to %s:%d\n", IOTEX_SPROUT_HTTP_HOST, ntohs(((struct sockaddr_in *)(res->ai_addr))->sin_port));

	close(_sock);
    _sock = -1;
}

void iotex_pal_sprout_http_server_disconnect(void)
{
    _pal_sprout_http_server_disconnect();
}

int iotex_pal_sprout_init(char *deviceDID, char *deviceKAKID)
{
    if (NULL == deviceDID || NULL == deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    _deviceDID   = deviceDID;
    _deviceKAKID = deviceKAKID;

    _pal_sprout_ctx_init();

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
    memset(_didcommToken, 0, IOTEX_PAL_SPROUT_HTTP_HEADER_MAX_SIZE);
    memcpy(_didcommToken, IOTEX_SPROUT_HTTP_HEADER_HEAD, strlen(IOTEX_SPROUT_HTTP_HEADER_HEAD));
#endif

    _err_times = 0;

    cJSON * client_id = cJSON_CreateObject();
    if (NULL == client_id)
        return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
    cJSON_AddStringToObject(client_id, "clientID", _deviceDID);
    _client_id_serialize = cJSON_PrintUnformatted(client_id);
    cJSON_Delete(client_id);
    if (NULL == _client_id_serialize)
        return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;

    LOG_INF("client_id : %s", _client_id_serialize);    

    memset(_messageID, 0, IOTEX_PAL_SPROUT_MESSAGE_ID_MAX_SIZE);
#else
    
    uint8_t signature[64] = {0};
    char signature_str[64 * 2 + 2 + 1] = {0};

    signature_str[0] = '0';
    signature_str[1] = 'x';

    cJSON_AddStringToObject(client_id, "deviceID", _deviceDID);
    _client_id_serialize = cJSON_PrintUnformatted(client_id);
    if (NULL == _client_id_serialize)
        return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;

    size_t  signature_length;
    psa_status_t status = iotex_pal_crypt_ecdsa_sign(_client_id_serialize, strlen(_client_id_serialize), signature, &signature_length);
    if (PSA_SUCCESS != status) {
        LOG_ERR("Failed to Signature - %d", status);
        return IOTEX_SPROUT_ERR_SIGNATURE_FAIL;
    }
        
    iotex_utils_convert_hex_to_str(signature , signature_length, signature_str + 2);

    cJSON_AddStringToObject(client_id, "signature", signature_str);

    _sprout_query = cJSON_PrintUnformatted(client_id);
    if (NULL == _sprout_query)
        return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;   

    LOG_INF("Query : %s", _sprout_query); 

#endif
    k_mutex_init(&_sprout_ctx._sprout_mutex);
    k_work_init(&_sprout_ctx.work, _pal_sprout_http_response_parse);
 
    return IOTEX_SPROUT_ERR_SUCCESS;
}

char *iotex_pal_sprout_message_id_get(void)
{
    if (_messageID[0])
        return _messageID;

    return NULL;        
}

static int _pal_sprout_http_response_recv(struct http_response *rsp, enum http_final_call final_data)
{
    int ret = IOTEX_SPROUT_ERR_SUCCESS;

    if (NULL == rsp)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    if (0 == rsp->content_length) {
        goto exit;
    }

    if ((NULL == _sprout_ctx._replyData)) {
        _sprout_ctx._replyData = calloc(rsp->content_length + 1, 1);
    }

    if ((NULL == _sprout_ctx._replyData)) {
        ret =  IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;
        goto exit;
    }

    if (rsp->body_found) {
        memcpy(_sprout_ctx._replyData + rsp->processed - rsp->body_frag_len, rsp->recv_buf + rsp->data_len - rsp->body_frag_len, rsp->body_frag_len);    
    }

exit:

#if 0
    LOG_INF("rsp->processed : %d", rsp->processed);
    LOG_INF("rsp->body_frag_len : %d", rsp->body_frag_len);
    LOG_INF("rsp->data_len : %d", rsp->data_len);
    LOG_INF("rsp->body_found : %d", rsp->body_found);

    printf("rsp->recv_buf : \n%s\n", rsp->recv_buf);

    memset(_replyData, 0, sizeof(_replyData));
#endif

    return ret;  
}

static void _response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    int ret = _pal_sprout_http_response_recv(rsp, final_data);
    if (IOTEX_SPROUT_ERR_SUCCESS != ret) {
        LOG_ERR("_pal_sprout_http_response_recv ret %d", ret);
        return;
    }
    
	if (final_data == HTTP_DATA_MORE)
        return;

    if (200 == rsp->http_status_code) {
        if (0 == strcmp(user_data, IOTEX_HTTP_USER_DATA_STRING_QUERY)) {
            _sprout_ctx.isRegister = 1;     
        }

        goto exit;
    }

    _sprout_ctx.err_times++;

    LOG_ERR("Response status : %s", rsp->http_status);
    if (_sprout_ctx._replyData) {
        LOG_ERR("Response body : %s", _sprout_ctx._replyData);
    }

    if (NULL == user_data)
        return;

#if 0
    if ((0 == strcmp(user_data, IOTEX_HTTP_USER_DATA_STRING_UPLOAD_CONFIG)) || (0 == strcmp(user_data, IOTEX_HTTP_USER_DATA_STRING_QUERY)) || (0 == strcmp(user_data, IOTEX_HTTP_USER_DATA_STRING_REQUEST_PUBKEY))) {
        _sprout_ctx.isRegister = 0; 
    }
#else
    if ( (0 != strcmp(user_data, IOTEX_HTTP_USER_DATA_STRING_SEND_MESSAGE)) ) {
        _sprout_ctx.isRegister = 0; 
    }
#endif

    return;        
#if 0
    cJSON *device_status =  cJSON_Parse(_sprout_ctx._replyData); 
    if (NULL == device_status)
        return;

    cJSON *ota_url_item = cJSON_GetObjectItem(device_status, "url");
    if (NULL == ota_url_item || !cJSON_IsString(ota_url_item))   
        return;

    if (_ota_url) 
        free (_ota_url);

    _ota_url = calloc(strlen(ota_url_item->valuestring) + 1, sizeof(char));
    if (NULL == _ota_url)
        return;

    strcpy(_ota_url, ota_url_item->valuestring);
#endif

exit:
    k_work_submit(&_sprout_ctx.work);
        
    return;
}

int iotex_pal_sprout_server_pubkey_get(void)
{
    if (-1 == _sock)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    _pal_sprout_ctx_deinit();
    _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_SERVER_PUBKEY;        

	struct http_request req = {0};

    req.method          = HTTP_GET;
    req.host            = IOTEX_SPROUT_HTTP_HOST;
    req.url             = IOTEX_SPROUT_HTTP_PATH_GET_PUBLIC_KEY;
    req.protocol        = "HTTP/1.1";
    req.response        = _response_cb;
    req.recv_buf        = _replyData;
    req.recv_buf_len    = sizeof(_replyData);

    memset(_replyData, 0, sizeof(_replyData));

    int ret = http_client_req(_sock, &req, IOTEX_SPROUT_HTTP_TIMEOUT, IOTEX_HTTP_USER_DATA_STRING_REQUEST_PUBKEY);
	if (ret < 0)
        return IOTEX_SPROUT_ERR_INTERNAL;

    return IOTEX_SPROUT_ERR_SUCCESS;
}

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
int iotex_pal_sprout_server_diddoc_get(void)
{
    if (-1 == _sock)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    LOG_INF("Starting to get DIDDoc of Server");

    _pal_sprout_ctx_deinit();
    _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_SERVER_DIDDOC;        

	struct http_request req = {0};

    req.method          = HTTP_GET;
    req.host            = IOTEX_SPROUT_HTTP_HOST;
    req.url             = IOTEX_SPROUT_HTTP_PATH_GET_DIDDOC;
    req.protocol        = "HTTP/1.1";
    req.response        = _response_cb;
    req.recv_buf        = _replyData;
    req.recv_buf_len    = sizeof(_replyData);

    memset(_replyData, 0, sizeof(_replyData));

    int ret = http_client_req(_sock, &req, IOTEX_SPROUT_HTTP_TIMEOUT, "GET_DIDDoc");
	if (ret < 0)
        return IOTEX_SPROUT_ERR_INTERNAL;

    return IOTEX_SPROUT_ERR_SUCCESS;
}

int iotex_pal_sprout_server_request_token(void)
{
    if (_err_times >= IOTEX_PAL_SPROUT_ERROR_TIMES_MAX)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (-1 == _sock)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
    
    if (NULL == _deviceDID || NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
    
    if (NULL == _client_id_serialize) {
        cJSON * client_id = cJSON_CreateObject();
        if (NULL == client_id)
            return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;

        cJSON_AddStringToObject(client_id, "clientID", _deviceDID);
        _client_id_serialize = cJSON_PrintUnformatted(client_id);
        if (NULL == _client_id_serialize)
            return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;

        cJSON_Delete(client_id); 
    }

    LOG_INF("Starting to request token");

    _pal_sprout_ctx_deinit();
    _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_REQUEST_TOKEN;              

	struct http_request req = {0};

    req.method          = HTTP_POST;
    req.url             = IOTEX_SPROUT_HTTP_PATH_REQUEST_TOKEN;
    req.host            = IOTEX_SPROUT_HTTP_HOST;
    req.port            = IOTEX_SPROUT_HTTP_PORT_STRING;
    req.protocol        = "HTTP/1.1";
    req.response        = _response_cb;
    req.recv_buf        = _replyData;
    req.recv_buf_len    = sizeof(_replyData);

    req.payload         = _client_id_serialize;
    req.payload_len     = strlen(_client_id_serialize);
    
    http_client_req(_sock, &req, IOTEX_SPROUT_HTTP_TIMEOUT, "Request_Token");       

    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif

int iotex_pal_sprout_send_message(char *message, bool isMessage)
{
    char *user_data = IOTEX_HTTP_USER_DATA_STRING_SEND_MESSAGE;

    if (_err_times >= IOTEX_PAL_SPROUT_ERROR_TIMES_MAX)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (-1 == _sock)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
    
    if (NULL == _deviceDID || NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS + 10;

    if (NULL == message)
        return IOTEX_SPROUT_ERR_BAD_STATUS + 11;

    LOG_INF("Message : %s", message);
    
#if 0
    if ((_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE) != IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE )
        return IOTEX_SPROUT_ERR_BAD_STATUS + 12;
#endif
    _pal_sprout_ctx_deinit();

    if (isMessage) {
        LOG_INF("Starting to send message");
        _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_SEND_MESSAGE;    
    } else {
        LOG_INF("Starting to upload config");
        _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_CONFIG_UPLOAD;  
        user_data = IOTEX_HTTP_USER_DATA_STRING_UPLOAD_CONFIG;  
    }

	struct http_request req = {0};

    req.method          = HTTP_POST;
    req.url             = IOTEX_SPROUT_HTTP_PATH_SEND_SENSOR_DATA;
    req.host            = IOTEX_SPROUT_HTTP_HOST;
    req.port            = IOTEX_SPROUT_HTTP_PORT_STRING;
    req.protocol        = "HTTP/1.1";
    req.response        = _response_cb;
    req.recv_buf        = _replyData;
    req.recv_buf_len    = sizeof(_replyData);

    req.payload         = message;
    req.payload_len     = strlen(message);

    int ret = http_client_req(_sock, &req, IOTEX_SPROUT_HTTP_TIMEOUT, user_data);        
    if (ret < 0) {
        LOG_ERR("http_client_req (%d)", ret);
        return ret;
    }

    return IOTEX_SPROUT_ERR_SUCCESS;
}

#ifdef IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
int iotex_pal_sprout_msg_query(char *message_id)
{
    char message_path[IOTEX_PAL_SPROUT_MESSAGE_ID_MAX_SIZE] = {0};

    if (_err_times >= IOTEX_PAL_SPROUT_ERROR_TIMES_MAX)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (-1 == _sock)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
    
    if (NULL == _deviceDID || NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (NULL == message_id && 0 == _messageID[0])
        return IOTEX_SPROUT_ERR_BAD_STATUS;             
    
    if (0 == _didcommToken[0])
        return IOTEX_SPROUT_ERR_BAD_STATUS;        

    memcpy(message_path, IOTEX_SPROUT_HTTP_MESSAGE_QUERY_PATH_HEAD, strlen(IOTEX_SPROUT_HTTP_MESSAGE_QUERY_PATH_HEAD));
    if (message_id)        
        memcpy(message_path + strlen(IOTEX_SPROUT_HTTP_MESSAGE_QUERY_PATH_HEAD), message_id, strlen(message_id));
    else
        memcpy(message_path + strlen(IOTEX_SPROUT_HTTP_MESSAGE_QUERY_PATH_HEAD), _messageID, strlen(_messageID));
	
    _pal_sprout_ctx_deinit();
    _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_QUERY_STATUS; 

	struct http_request req = {0};

    req.method          = HTTP_GET;
    req.url             = message_path;
    req.host            = IOTEX_SPROUT_HTTP_HOST;
    req.port            = IOTEX_SPROUT_HTTP_PORT_STRING;
    req.protocol        = "HTTP/1.1";
    req.response        = _response_cb;
    req.recv_buf        = _replyData;
    req.recv_buf_len    = sizeof(_replyData);

    req.header_fields = (const char **)_headers;

    http_client_req(_sock, &req, IOTEX_SPROUT_HTTP_TIMEOUT, "Query");

    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif

int iotex_pal_sprout_state_query(void)
{
    if (_err_times >= IOTEX_PAL_SPROUT_ERROR_TIMES_MAX)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
    
    if (-1 == _sock)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
    
#if 0        
    if (IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE != _sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE)
        return IOTEX_SPROUT_ERR_BAD_STATUS;   
#endif 

    _pal_sprout_ctx_deinit();
    _sprout_ctx.type = IOTEX_PAL_SPROUT_CTX_TYPE_DEVICE_QUERY; 

	struct http_request req = {0};

    req.method          = HTTP_GET;
    req.url             = IOTEX_SPROUT_HTTP_PATH_DEVICE_QUERY;
    req.host            = IOTEX_SPROUT_HTTP_HOST;
    req.port            = IOTEX_SPROUT_HTTP_PORT_STRING;
    req.protocol        = "HTTP/1.1";
    req.response        = _response_cb;
    req.recv_buf        = _replyData;
    req.recv_buf_len    = sizeof(_replyData);

    req.payload         = _sprout_query;
    req.payload_len     = strlen(_sprout_query);

    http_client_req(_sock, &req, IOTEX_SPROUT_HTTP_TIMEOUT, IOTEX_HTTP_USER_DATA_STRING_QUERY);

    return IOTEX_SPROUT_ERR_SUCCESS;
}

static int _pal_sprout_config_upload(void)
{
    uint8_t payload[300] = {0};
    int rc;
    char *config_data = NULL;

    rc = packDevConf(payload, sizeof(payload));
    if (rc) {
        config_data = base64_encode_automatic( payload, rc );
        if (config_data) {
            iotex_pal_sprout_didcomm_send_message(config_data, false);
        }
        else {
            LOG_ERR("Failed to Send Config Package");

            return IOTEX_SPROUT_ERR_CONFIG_UPLOAD;
        }

        LOG_INF("Success to Send Config Package : %d \n", rc);
    } else {
        LOG_ERR("Config package error ! \n");

        return IOTEX_SPROUT_ERR_CONFIG_UPLOAD;
    }

    if (config_data)
        free (config_data);    

    return IOTEX_SPROUT_ERR_SUCCESS;
}

static int _pal_sprout_didcomm_prepare_server_pubkey(void)
{
    uint8_t err_times = 0;

    LOG_INF("Starting to get public key of Server");

    while (!(_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_REQUEST_PUBKEY)) {

        if (++err_times > 3)
            return IOTEX_SPROUT_ERR_REQUEST_PUBKEY_FAIL;

        iotex_pal_sprout_server_pubkey_get();

        k_sleep(K_MSEC(1000));

    }
    
    return IOTEX_SPROUT_ERR_SUCCESS;
}

static int _pal_sprout_didcomm_prepare_query_status(void)
{
    uint8_t err_times = 0;

    LOG_INF("Starting to query the status of device");

    while (!(_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_REQUEST_TIMESTAMP)) {

        if (++err_times > 3)
            return IOTEX_SPROUT_ERR_QUERY_FAIL;

        iotex_pal_sprout_state_query();

        k_sleep(K_MSEC(1000));

    }
    
    return IOTEX_SPROUT_ERR_SUCCESS;
}

#ifdef IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
static int _pal_sprout_didcomm_prepare_diddoc_of_server(void)
{
    uint8_t err_times = 0;

    while (!(_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_GET_DIDDOC_OF_SERVER)) {

        if (++err_times >= 3)
            return IOTEX_SPROUT_ERR_DIDDOC_FROM_SERVER;

        iotex_pal_sprout_server_diddoc_get();

        k_sleep(K_MSEC(500));

    }
    
    return IOTEX_SPROUT_ERR_SUCCESS;
}

static int _pal_sprout_didcomm_prepare_request_token(void)
{
    uint8_t err_times = 0;

    while (!(_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_REQUEST_TOKEN)) {

        if (err_times++ >= 3)
            return IOTEX_SPROUT_ERR_REQUEST_TOKEN;

        iotex_pal_sprout_server_request_token();

        k_sleep(K_MSEC(500));

    }
    
    return IOTEX_SPROUT_ERR_SUCCESS;
}
#endif

int iotex_pal_sprout_is_ready_check(void)
{
    if (0 == _sprout_ctx.isRegister)
        return IOTEX_PAL_SPROUT_DEVICE_READY_NOT_REGISTER;

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
    if (0 == (_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_GET_DIDDOC_OF_SERVER))
        return IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_DIDDOC;

    if (0 == (_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_REQUEST_TOKEN))
        return IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_TOKEN;
#else
    if (0 == (_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_REQUEST_PUBKEY))
        return IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_PUBKEY;

    if (0 == (_sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_REQUEST_TIMESTAMP))
        return IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_TIMESTAMP;        
#endif 
    return IOTEX_PAL_SPROUT_DEVICE_READY_OK;
}

int iotex_pal_sprout_didcomm_prepare(void)
{
    int ret = _pal_sprout_http_server_connect();
    if (ret)
        return ret;

    if (_sock < 0)
        return -1;

    ret = _pal_sprout_didcomm_prepare_server_pubkey();
    if (ret)
        goto exit;        

#ifdef IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
    ret = _pal_sprout_didcomm_prepare_diddoc_of_server();
    if (ret)
        goto exit;        

    ret = _pal_sprout_didcomm_prepare_request_token();
    if (ret)
        goto exit;
#else
    ret = _pal_sprout_config_upload();        
    if (ret) {
        LOG_ERR("_pal_sprout_config_upload err : %d", ret);
        goto exit;
    }

    ret = _pal_sprout_didcomm_prepare_query_status();
    if (ret) {
        LOG_ERR("iotex_pal_sprout_state_query err : %d", ret);
    }
#endif

exit:
    _pal_sprout_http_server_disconnect();

    return ret;
}

#if IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM
int iotex_pal_sprout_didcomm_send_message(char *message, bool isMessage)
{
    if (NULL == message)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    if (_sock < 0)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (NULL == _deviceDID || NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (0 == _serverKAKID[0])
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (0 == _sprout_ctx.isRegister)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE != _sprout_ctx.status & IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    char *recipients_kid[JOSE_JWE_RECIPIENTS_MAX] = {0};
    recipients_kid[0] = _serverKAKID;

    char *jwe_json = iotex_jwe_encrypt(message, Ecdh1puA256kw, A256cbcHs512, _deviceDID, NULL, recipients_kid, false);
    if (NULL == jwe_json) {
        LOG_ERR("Failed to Encrypt the message\n");
        return IOTEX_SPROUT_ERR_ENCRYPT_FAIL;
    }

    int ret = iotex_pal_sprout_send_message(jwe_json, isMessage);
    if (IOTEX_SPROUT_ERR_SUCCESS != ret)
        LOG_ERR("Failed to Send Message to the Server (%d)", ret);
    
    free (jwe_json);

    return ret;
}
#else
int iotex_pal_sprout_didcomm_send_message(char *message, bool isMessage)
{
    int ret = IOTEX_SPROUT_ERR_SUCCESS;

    if (NULL == message)
        return IOTEX_SPROUT_ERR_BAD_INPUT_PARA;

    if (_sock < 0)
        return IOTEX_SPROUT_ERR_BAD_STATUS;

    if (NULL == _deviceDID || NULL == _deviceKAKID)
        return IOTEX_SPROUT_ERR_BAD_STATUS;
         
    cJSON * upload_json = cJSON_CreateObject();
    if (NULL == upload_json)
        return IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;

    cJSON_AddStringToObject(upload_json, "deviceID", _deviceDID);
    cJSON_AddStringToObject(upload_json, "payload", message);

    char *upload_json_serialize = cJSON_PrintUnformatted(upload_json);
    if (NULL == upload_json_serialize) {
        ret = IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;
        goto exit;
    }

    uint8_t signature[64] = {0};
    char signature_str[64 * 2 + 2 + 1] = {0};

    signature_str[0] = '0';
    signature_str[1] = 'x';

    size_t  signature_length;
    psa_status_t status = iotex_pal_crypt_ecdsa_sign(upload_json_serialize, strlen(upload_json_serialize), signature, &signature_length);
    if (PSA_SUCCESS != status) {
        LOG_ERR("Failed to Signature - %d", status);
        ret = IOTEX_SPROUT_ERR_SIGNATURE_FAIL;
        goto exit_1;
    }
        
    iotex_utils_convert_hex_to_str(signature , signature_length, signature_str + 2);

    cJSON_AddStringToObject(upload_json, "signature", signature_str);

    char * sprout_message_serialize = cJSON_PrintUnformatted(upload_json);
    if (NULL == sprout_message_serialize) {
        ret = IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY;   
        goto exit_2;
    }

    ret = iotex_pal_sprout_send_message(sprout_message_serialize, isMessage);
    if (IOTEX_SPROUT_ERR_SUCCESS != ret)
        LOG_ERR("Failed to Send Message to the Server (%d)", ret);
    
exit_2:   
    if (sprout_message_serialize) {
        free (sprout_message_serialize);
    }  

exit_1:
    if (upload_json_serialize) {
        free (upload_json_serialize);  
    }

exit:    
    cJSON_Delete(upload_json);

    return ret;
}
#endif

int iotex_pal_sprout_loop(void)
{
    int ret = k_mutex_lock(&_sprout_ctx._sprout_mutex, K_MSEC(5000));
    if (ret) {
        LOG_ERR("Get Sprout Mutex Timeout");
        return IOTEX_SPROUT_ERR_TIMEOUT;
    }

    iotex_pal_sprout_didcomm_send_message(NULL, true);

    return IOTEX_SPROUT_ERR_SUCCESS;
}
