#ifndef __IOCONNECT_PAL_SPROUT_H__
#define __IOCONNECT_PAL_SPROUT_H__

#define SPROUT_COMMUNICATE_TYPE_NORMAL      1
#define SPROUT_COMMUNICATE_TYPE_DID         2

#define IOTEX_SPROUT_COMMUNICATE_TYPE       SPROUT_COMMUNICATE_TYPE_DID

// #define IOTEX_SPROUT_SERVER_UES_TEST_NET 
#ifdef IOTEX_SPROUT_SERVER_UES_TEST_NET
#define IOTEX_SPROUT_HTTP_HOST                  "pebble-server.testnet.iotex.io"
#define IOTEX_PEBBLE_IOID_PROJECT_ID            "923"
#else
#define IOTEX_SPROUT_HTTP_HOST                  "pebble-server.mainnet.iotex.io"
#define IOTEX_PEBBLE_IOID_PROJECT_ID            "6"
#endif

#define IOTEX_SPROUT_HTTP_PORT_STRING           "80"         
#define IOTEX_SPROUT_HTTP_PORT_DEMICAL          80
#define IOTEX_SPROUT_HTTPS_PORT_STRING         "443"
#define IOTEX_SPROUT_HTTPS_PORT_DEMICAL         443

#define IOTEX_SPROUT_HTTP_TIMEOUT               5000

#define IOTEX_SPROUT_HTTP_URL                   "/didDoc"
#define IOTEX_SPROUT_HTTP_PATH_MESSAGE          "/message"
#define IOTEX_SPROUT_HTTP_PATH_REQUEST_TOKEN    "/issue_vc"
#define IOTEX_SPROUT_HTTP_PATH_GET_DIDDOC       "/didDoc"
#if 0
#define IOTEX_SPROUT_HTTP_PATH_DEVICE_QUERY     "/device/query"
#define IOTEX_SPROUT_HTTP_PATH_SEND_SENSOR_DATA "/device/data"
#else
// #define IOTEX_SPROUT_HTTP_PATH_DEVICE_QUERY     "/device"
#define IOTEX_SPROUT_HTTP_PATH_DEVICE_QUERY     "/v2/device"
// #define IOTEX_SPROUT_HTTP_PATH_SEND_SENSOR_DATA "/device"
#define IOTEX_SPROUT_HTTP_PATH_SEND_SENSOR_DATA "/v2/device"
// #define IOTEX_SPROUT_HTTP_PATH_SEND_SENSOR_DATA "/task"
#define IOTEX_SPROUT_HTTP_PATH_GET_PUBLIC_KEY   "/public_key"
#endif
#define IOTEX_SPROUT_HTTP_HEADER_HEAD           "Authorization: Bearer "
#define IOTEX_SPROUT_HTTP_HEADER_END            "\r\n"
#define IOTEX_SPROUT_HTTP_MESSAGE_QUERY_PATH_HEAD           "/message/"

#define REQUEST "GET /didDoc HTTP/1.1\r\nHost: sprout-testnet.w3bstream.com\r\nConnection: close\r\n\r\n"

#define SPROUT_HTTP_RESPONSE_DATA_TYPE_SEND          1
#define SPROUT_HTTP_RESPONSE_DATA_TYPE_JWT           2
#define SPROUT_HTTP_RESPONSE_DATA_TYPE_DIDDOC        3
#define SPROUT_HTTP_RESPONSE_DATA_TYPE_QUERY         4

#define SPROUT_QUERY_PATH_SIZE              128
#define SPROUT_DID_TOKEN_SIZE               1024

#define IOTEX_PAL_SPROUT_SERVER_KA_KID_MAX_SIZE     128
#define IOTEX_PAL_SPROUT_QUERY_PATH_MAX_SIZE        128
#define IOTEX_PAL_SPROUT_MESSAGE_ID_MAX_SIZE        256
#define IOTEX_PAL_SPROUT_HTTP_REPLY_BUF_MAX_SIZE    256     // 1024 * 8
#define IOTEX_PAL_SPROUT_HTTP_HEADER_MAX_SIZE       1024

#define IOTEX_PEBBLE_SENSOR_DATA_BUFFER_SIZE        512

#define IOTEX_SPROUT_ERR_SUCCESS                 0
#define IOTEX_SPROUT_ERR_BAD_INPUT_PARA         -1
#define IOTEX_SPROUT_ERR_DATA_FORMAT            -2
#define IOTEX_SPROUT_ERR_INSUFFICIENT_MEMORY    -3
#define IOTEX_SPROUT_ERR_BAD_STATUS             -4
#define IOTEX_SPROUT_ERR_ENCRYPT_FAIL           -5
#define IOTEX_SPROUT_ERR_TIMEOUT                -6
#define IOTEX_SPROUT_ERR_INTERNAL               -7
#define IOTEX_SPROUT_ERR_DIDDOC_FROM_SERVER     -8
#define IOTEX_SPROUT_ERR_REQUEST_TOKEN          -9
#define IOTEX_SPROUT_ERR_CONFIG_UPLOAD          -10
#define IOTEX_SPROUT_ERR_SIGNATURE_FAIL         -11
#define IOTEX_SPROUT_ERR_VERIFY_FAIL            -12
#define IOTEX_SPROUT_ERR_REQUEST_PUBKEY_FAIL    -13
#define IOTEX_SPROUT_ERR_IMPORT_PUBKEY_FAIL     -14
#define IOTEX_SPROUT_ERR_QUERY_FAIL             -15

// #define IOTEX_PAL_SPROUT_STATUS_INIT                    0
// #define IOTEX_PAL_SPROUT_STATUS_SERVER_DIDDOC_GET       1
// #define IOTEX_PAL_SPROUT_STATUS_HTTP_TOKEN_GOTTEN       2
// #define IOTEX_PAL_SPROUT_STATUS_MESSAGE_ID_GOTTEN       3
// #define IOTEX_PAL_SPROUT_STATUS_MESSAGE_QUERY_GOTTEN    4

#define IOTEX_PAL_SPROUT_CTX_TYPE_INIT                  0
#define IOTEX_PAL_SPROUT_CTX_TYPE_SERVER_DIDDOC         1
#define IOTEX_PAL_SPROUT_CTX_TYPE_SERVER_PUBKEY         2
#define IOTEX_PAL_SPROUT_CTX_TYPE_REQUEST_TOKEN         3
#define IOTEX_PAL_SPROUT_CTX_TYPE_SEND_MESSAGE          4
#define IOTEX_PAL_SPROUT_CTX_TYPE_CONFIG_UPLOAD         5
#define IOTEX_PAL_SPROUT_CTX_TYPE_QUERY_STATUS          6
#define IOTEX_PAL_SPROUT_CTX_TYPE_DEVICE_QUERY          7

#define IOTEX_PAL_SPROUT_ERROR_TIMES_MAX                3

#define IOTEX_PAL_SPROUT_STATUS_INIT                    0x00
#define IOTEX_PAL_SPROUT_STATUS_GET_DIDDOC_OF_SERVER    0x01
#define IOTEX_PAL_SPROUT_STATUS_REQUEST_TOKEN           0x02
#define IOTEX_PAL_SPROUT_STATUS_REQUEST_PUBKEY          0x04
#define IOTEX_PAL_SPROUT_STATUS_REQUEST_TIMESTAMP       0x08

#define IOTEX_PAL_SPROUT_DEVICE_READY_OK                    0
#define IOTEX_PAL_SPROUT_DEVICE_READY_NOT_REGISTER          1
#define IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_DIDDOC       2
#define IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_TOKEN        3
#define IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_PUBKEY       4
#define IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_TIMESTAMP    5

#define IOTEX_PAL_SPROUT_STATUS_VALID_TO_SEND_MESSAGE   (IOTEX_PAL_SPROUT_STATUS_GET_DIDDOC_OF_SERVER | IOTEX_PAL_SPROUT_STATUS_REQUEST_TOKEN)


int iotex_pal_sprout_init(char *deviceDID, char *deviceKAKID, char *deviceID);
int iotex_pal_sprout_loop(void);
int iotex_pal_sprout_http_server_connect(void);
int iotex_pal_sprout_server_request_token(void);
int iotex_pal_sprout_send_message(char *message, bool isMessage);
int iotex_pal_sprout_msg_query(char *message_id);
int iotex_pal_sprout_didcomm_send_message(char *message, bool isMessage);
int iotex_pal_sprout_is_ready_check(void);

#endif
