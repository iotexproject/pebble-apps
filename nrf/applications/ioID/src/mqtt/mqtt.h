#ifndef  _IOTEX_MQTT_H_
#define  _IOTEX_MQTT_H_

#include <zephyr/net/mqtt.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#define DATA_BUFFER_SIZE   500

extern atomic_val_t send_data_enable;

struct mqtt_payload {
    char *buf;
    size_t len;
};

/* Get MQTT client id */
const uint8_t *iotex_mqtt_get_client_id(void);

int iotex_mqtt_client_init(struct mqtt_client *client, struct pollfd *fds);
int iotex_mqtt_publish_data(struct mqtt_client *client, enum mqtt_qos qos, char *data, int  len);

/* Bulk upload nvs stored data */
void iotex_mqtt_bulk_upload_sampling_data(uint16_t channel);

/* Sampling and store data to nvs */
bool iotex_mqtt_sampling_data_and_store(uint16_t channel);

/* Sampling data and pack seleccted data to json string to output buffer */
/*   wrap  json package */
/*  mqtt  heart beat */
int iotex_mqtt_heart_beat(struct mqtt_client *client, enum mqtt_qos qos);

int iotex_mqtt_configure_upload(struct mqtt_client *client, enum mqtt_qos qos);

int SensorPackage(uint16_t channel, uint8_t *buffer);

#endif
