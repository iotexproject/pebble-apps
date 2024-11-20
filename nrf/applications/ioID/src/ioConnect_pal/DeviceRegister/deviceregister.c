#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "include/utils/devRegister/devRegister.h"

#define MSG_SIZE 256

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 2, 4);

/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static unsigned int _sign_kID = 1;

static char rx_buf[MSG_SIZE];
static char tx_buf[MSG_SIZE];
static int  rx_buf_pos;

static char *upload_did    = NULL;
static char *upload_diddoc = NULL;

static uint8_t secret[32] = {0x57, 0x81, 0x5e, 0x3d, 0x20, 0x9a, 0x42, 0x8d, 0x48, 0x44, 0x83, 0xcc, 0x1a, 0x2c, 0x5b, 0x5d, 0x97, 0x00, 0x7d, 0x5f, 0x17, 0xff, 0xc0, 0xd4, 0xee, 0xd6, 0x03, 0xa4, 0x08, 0x55, 0x03, 0x9e};

static psa_key_id_t device_register_key_id = 0;
static uint8_t signature[64] = {0};
static char    signature_str[64 * 2 + 1] = {0};

/*
 * Print a null-terminated string character by character to the UART interface
 */
static void _print_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
static void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			/* terminate string */
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

int iotex_pal_device_register_init(char *deviceDID, char *deviceDIDDoc, unsigned int sign_kID)
{
    if (NULL == deviceDID || NULL == deviceDIDDoc) {
        return -1;
	}
    
	if (sign_kID) {
        _sign_kID = sign_kID;
	}

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return -1;
	}

	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}

		return ret;
	}

	uart_irq_rx_enable(uart_dev);

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_bits(&attributes, 256); 

    psa_status_t status = psa_import_key( &attributes, secret, sizeof(secret), &device_register_key_id );
    if (PSA_SUCCESS != status)
        return -2;	

	static size_t  signature_length = 0;
    status = psa_sign_message(device_register_key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), deviceDID, strlen(deviceDID), signature, sizeof(signature), &signature_length);
	if (PSA_SUCCESS != status)
		goto exit;

	iotex_utils_convert_hex_to_str(signature , signature_length, signature_str);		

	if (NULL == upload_did)
    	upload_did = iotex_utils_device_register_did_upload_prepare(deviceDID, _sign_kID, signature_str, true);

	if (NULL == upload_diddoc)
    	upload_diddoc = iotex_utils_device_register_diddoc_upload_prepare(deviceDIDDoc, _sign_kID, signature_str, true);

exit:

	psa_destroy_key(device_register_key_id);
	device_register_key_id = 0;
    
    return status;
}

int iotex_pal_device_register_loop(void)
{
    if (NULL == upload_did || NULL == upload_diddoc)
        return -1;

    if (0 == _sign_kID)
        return -1;

    while (k_msgq_get(&uart_msgq, &tx_buf, K_MSEC(500)) == 0) {

        if (0 == strcmp("getdid", tx_buf)) {

            _print_uart(upload_did);

        } else if (0 == strcmp("getdiddoc", tx_buf)) {

            _print_uart(upload_diddoc);

		} else if (0 == strcmp("quit", tx_buf)) {
			return 1;
        // } else if (0 == strncmp("S", tx_buf, 1)) {
		} else {
            // char *sign = iotex_utils_device_register_signature_response_prepare(tx_buf + 1, _sign_kID);
			char *sign = iotex_utils_device_register_signature_response_prepare(tx_buf, _sign_kID);
			if (sign) {
            	_print_uart(sign);
			} else {
				_print_uart("{\n\t\"Sign\":\t\"Failed to Signature\"\n}");
			}
        }
    }

	return 0;  
}



