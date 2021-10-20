#include <stdio.h>
#include <zephyr.h>
#include <net/socket.h>
#include <power/reboot.h>
#include "mqtt.h"
#include "config.h"
#include "hal/hal_gpio.h"
#include "modem/modem_helper.h"
#include "mqtt/devReg.h"
#include "ui.h"
#include "display.h"

#include "pb_decode.h"
#include "pb_encode.h"
#include "package.pb.h"

#if !defined(CONFIG_CLOUD_CLIENT_ID)
#define CLIENT_ID_LEN (MODEM_IMEI_LEN + 4)
#else
#define CLIENT_ID_LEN (sizeof(CONFIG_CLOUD_CLIENT_ID) - 1)
#endif

#define CLOUD_CONNACK_WAIT_DURATION	60 /*CONFIG_CLOUD_WAIT_DURATION*/


#define  MQTT_TOPIC_SIZE    100

static bool connected = 0;

/* When MQTT_EVT_CONNACK callback enable data sending */
atomic_val_t send_data_enable;

/* When connect mqtt server failed, auto reboot */
static struct k_delayed_work cloud_reboot_work;

static uint8_t devSt = 0;

/* Buffers for MQTT client. */
static u8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

static uint32_t cnt = 0;

extern void mqttGetResponse(void);
extern void sendHearBeat(void);


const uint8_t *pmqttBrokerHost = "a11homvea4zo8t-ats.iot.ap-east-1.amazonaws.com";

static  void iotex_mqtt_get_topic(u8_t *buf, int len) 
{
    snprintf(buf, len, "device/%s/data",iotex_mqtt_get_client_id());
}

static void iotex_mqtt_get_config_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "backend/%s/firmware",iotex_mqtt_get_client_id());
}

static void iotex_mqtt_get_upgrdOver_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "device/%s/action/download-complate",iotex_mqtt_get_client_id());
}

static void iotex_get_heart_beat_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "device/%s/action/update-state",iotex_mqtt_get_client_id());
}

static void iotex_set_upload_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "device/%s/config",iotex_mqtt_get_client_id());
}

static void iotex_mqtt_get_reg_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "backend/%s/status",iotex_mqtt_get_client_id());
}

static void iotex_mqtt_get_ownership_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "device/%s/confirm",iotex_mqtt_get_client_id());
}

static void iotex_mqtt_query_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "device/%s/query",iotex_mqtt_get_client_id());
}

static void  iotex_mqtt_backend_ack_topic(u8_t *buf, int len)
{
    snprintf(buf, len, "backend/%s/status",iotex_mqtt_get_client_id());
}

static int packDevState(uint8_t *buf, uint32_t size)
{
    SensorState devSta = SensorState_init_zero;
    BinPackage binpack = BinPackage_init_zero;
    uint32_t uint_timestamp;
    char esdaSign[65];
    int sinLen;

    switch (devSt) {
        case 0:
            //snprintf(buf, size, "{\"message\":{\"deviceState\":\"%s\"}}","online");
            devSta.has_state = true;
            strcpy(devSta.state, "1");
            devSt = 1;
            break;
         case 1:
            //snprintf(buf, size, "{\"message\":{\"deviceState\":\"%s\"}}","running");
            devSta.has_state = true;
            strcpy(devSta.state, "2");
            break;
        case 2:
            //snprintf(buf, size, "{\"message\":{\"deviceState\":\"%s\"}}","offline");
            devSta.has_state = true;
            strcpy(devSta.state, "3");
            break;
        case 3:
            //snprintf(buf, size, "{\"message\":{\"deviceState\":\"%s\"}}","error");
            devSta.has_state = true;
            strcpy(devSta.state, "4");
            break;
        default:
            break;
    }

    if (devSt < 4) {
        uint_timestamp = atoi(iotex_modem_get_clock(NULL));
        pb_ostream_t enc_datastream;
        enc_datastream = pb_ostream_from_buffer(binpack.data.bytes, sizeof(binpack.data.bytes));
        if (!pb_encode(&enc_datastream, SensorState_fields, &devSta)) {
            printk("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_datastream));
            return 0;
        }

        binpack.data.size = enc_datastream.bytes_written;
        binpack.data.bytes[enc_datastream.bytes_written] = (char)((uint_timestamp & 0xFF000000) >> 24);
        binpack.data.bytes[enc_datastream.bytes_written + 1] = (char)((uint_timestamp & 0x00FF0000) >> 16);
        binpack.data.bytes[enc_datastream.bytes_written + 2] = (char)((uint_timestamp & 0x0000FF00) >> 8);
        binpack.data.bytes[enc_datastream.bytes_written + 3] = (char)(uint_timestamp & 0x000000FF);
        doESDASign(binpack.data.bytes,enc_datastream.bytes_written + 4, esdaSign, &sinLen);
        memcpy(binpack.signature, esdaSign, 64);
        binpack.timestamp = uint_timestamp;

        pb_ostream_t enc_packstream;
        enc_packstream = pb_ostream_from_buffer(buf, size);
        if (!pb_encode(&enc_packstream, BinPackage_fields, &binpack)) {
            printk("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_packstream));
            return 0;
        }
        return enc_packstream.bytes_written;
    }
    return 0;
}

static void packUpgrdOver(uint8_t *buf, uint32_t size)
{
    //snprintf(buf, size, "{\"message\":{\"downloadStatus\":\"1\"}}");
}

/*
 * @brief Resolves the configured hostname and
 * initializes the MQTT broker structure
 */
static void broker_init(const char *hostname, struct sockaddr_storage *storage) {
    int err;
    struct addrinfo *addr;
    struct addrinfo *result;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    err = getaddrinfo(hostname, NULL, &hints, &result);

    if (err) {
        printk("ERROR: getaddrinfo failed %d\n", err);
        return;
    }

    addr = result;
    err = -ENOENT;

    while (addr != NULL) {
        /* IPv4 Address. */
        if (addr->ai_addrlen == sizeof(struct sockaddr_in)) {
            struct sockaddr_in *broker = ((struct sockaddr_in *)storage);

            broker->sin_family = AF_INET;
            broker->sin_port = htons(CONFIG_MQTT_BROKER_PORT);
            broker->sin_addr.s_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr.s_addr;

            printk("IPv4 Address 0x%08x\n", broker->sin_addr.s_addr);
            break;
        }
        else if (addr->ai_addrlen == sizeof(struct sockaddr_in6)) {
            /* IPv6 Address. */
            struct sockaddr_in6 *broker = ((struct sockaddr_in6 *)storage);

            memcpy(broker->sin6_addr.s6_addr,
                   ((struct sockaddr_in6 *)addr->ai_addr)
                   ->sin6_addr.s6_addr,
                   sizeof(struct in6_addr));
            broker->sin6_family = AF_INET6;
            broker->sin6_port = htons(CONFIG_MQTT_BROKER_PORT);

            printk("IPv6 Address");
            break;
        } else {
            printk("error: ai_addrlen = %u should be %u or %u\n",
                   (unsigned int)addr->ai_addrlen,
                   (unsigned int)sizeof(struct sockaddr_in),
                   (unsigned int)sizeof(struct sockaddr_in6));
        }

        addr = addr->ai_next;
        break;
    }

    /* Free the address. */
    freeaddrinfo(result);
}

static int subscribe_config_topic(struct mqtt_client *client) {
    uint8_t topic[MQTT_TOPIC_SIZE];
    iotex_mqtt_get_config_topic(topic, sizeof(topic));
    struct mqtt_topic subscribe_topic = {
        .topic = {
            .utf8 = (uint8_t *)topic,
            .size = strlen(topic)
        },
        .qos = MQTT_QOS_1_AT_LEAST_ONCE
    };

    const struct mqtt_subscription_list subscription_list = {
        .list = &subscribe_topic,
        .list_count = 1,
        .message_id = 1234
    };

    printk("Subscribing to: %s len %u, qos %u\n",
           subscribe_topic.topic.utf8,
           subscribe_topic.topic.size,
           subscribe_topic.qos);

    return mqtt_subscribe(client, &subscription_list);
}

static int subscribe_regist_topic(struct mqtt_client *client) {
    uint8_t topic[MQTT_TOPIC_SIZE];
    iotex_mqtt_get_reg_topic(topic, sizeof(topic));
    struct mqtt_topic subscribe_topic = {
        .topic = {
            .utf8 = (uint8_t *)topic,
            .size = strlen(topic)
        },
        .qos = MQTT_QOS_1_AT_LEAST_ONCE
    };

    const struct mqtt_subscription_list subscription_list = {
        .list = &subscribe_topic,
        .list_count = 1,
        .message_id = 12345
    };

    printk("Subscribing to: %s len %u, qos %u\n",
           subscribe_topic.topic.utf8,
           subscribe_topic.topic.size,
           subscribe_topic.qos);

    return mqtt_subscribe(client, &subscription_list);
}


/**@brief Function to print strings without null-termination. */
static void data_print(u8_t *prefix, u8_t *data, size_t len) {
    char buf[len + 1];
    memcpy(buf, data, len);
    buf[len] = 0;
    printk("%s%s\n", prefix, buf);
}

/*
 * @brief Function to read the published payload.
 */
static int publish_get_payload(struct mqtt_client *c, u8_t *write_buf, size_t length) {
    u8_t *buf = write_buf;
    u8_t *end = buf + length;

    if (length > sizeof(payload_buf)) {
        return -EMSGSIZE;
    }

    while (buf < end) {
        int ret = mqtt_read_publish_payload_blocking(c, buf, end - buf);
        if (ret < 0) {
            return ret;
        }
        else if (ret == 0) {
            return -EIO;
        }

        buf += ret;
    }

    return 0;
}


/**@brief Reboot the device if CONNACK has not arrived. */
static void cloud_reboot_work_fn(struct k_work *work) {    
    if (++cnt >= CLOUD_CONNACK_WAIT_DURATION) {
        printk("[%s] MQTT Connect timeout reboot!\n", __func__);
        sys_reboot(0);
    }

    printk(".");
    k_delayed_work_submit(&cloud_reboot_work, K_SECONDS(1));
}

/**@brief MQTT client event handler */
static void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt) {

    int err;
    uint8_t revTopic[100];
    uint32_t status;
    switch (evt->type) {
        case MQTT_EVT_CONNACK:
            if (evt->result != 0) {
                printk("MQTT connect failed %d\n", evt->result);
                break;
            }
            /* Cancel reboot worker */
            k_delayed_work_cancel(&cloud_reboot_work);
            connected = 1;
            atomic_set(&send_data_enable, 1);

            //iotex_hal_gpio_set(LED_BLUE, LED_ON);
            //iotex_hal_gpio_set(LED_GREEN, LED_OFF);

            // upload configure and  firmware version
            //iotex_mqtt_configure_upload(c,0);
            //publish_dev_query("", 0);

            printk("[%s:%d] MQTT client connected!\n", __func__, __LINE__);
            if (IsDevReg()) {
                subscribe_regist_topic(c);
                ClearKey();
                devRegSet(DEV_REG_STATUS);
                //SetIndicator(UI_WAIT_FOR_WALLET);
                //hintString(htNetConnected,HINT_TIME_FOREVER);
            }
            else
            {
                if(devRegGet() == DEV_UPGRADE_ENTRY) {
                    devRegSet(DEV_UPGRADE_CONFIRM);
                }
            }
            // get upgrade url
            subscribe_config_topic(c);
            // send heartbeat
            //sendHearBeat();
            cnt = 0;
            sta_SetMeta(AWS_LINKER, STA_LINKER_ON);
            break;
        case MQTT_EVT_DISCONNECT:
            connected = 0;
            atomic_set(&send_data_enable, 0);
            //iotex_hal_gpio_set(LED_BLUE, LED_OFF);
            //iotex_hal_gpio_set(LED_GREEN, LED_ON);
            printk("[%s:%d] MQTT client disconnected %d\n", __func__, __LINE__, evt->result);
            break;
        case MQTT_EVT_PUBLISH: {
            const struct mqtt_publish_param *p = &evt->param.publish;
            err = publish_get_payload(c, payload_buf, p->message.payload.len);

            if (err >= 0) {
                data_print("Received: ", payload_buf, p->message.payload.len);
                //iotex_mqtt_get_reg_topic(revTopic,sizeof(revTopic));
                iotex_mqtt_backend_ack_topic(revTopic, sizeof(revTopic));
                if (!strcmp(p->message.topic.topic.utf8, revTopic)) {
                    status = iotex_mqtt_get_wallet(payload_buf, p->message.payload.len);
                    if (status == 2) {
                        iotex_mqtt_configure_upload(c, 0);
                        if (IsDevReg())
                            devRegSet(DEV_REG_SUCCESS);
                    } else if (status == 1) {
                        //if(devRegGet() == DEV_REG_POLL_FOR_WALLET)
                        if (devRegGet() < DEV_REG_ADDR_CHECK)
                            devRegSet(DEV_REG_ADDR_CHECK);
                    } else {
                        if (devRegGet() != DEV_REG_POLL_FOR_WALLET) {
                            hintString(htNetConnected,HINT_TIME_FOREVER);
                            devRegSet(DEV_REG_PRESS_ENTER);
                        }
                    }
                } else {   //  download  firmware url
                    iotex_mqtt_get_config_topic(revTopic,sizeof(revTopic));
                    if (!strcmp(p->message.topic.topic.utf8, revTopic)) {
                        if (!IsDevReg()) {
                            iotex_mqtt_update_url(payload_buf, p->message.payload.len);
                            hintString(htconfirmUpgrade,HINT_TIME_FOREVER);
                            devRegSet(DEV_UPGRADE_STARTED);
                        }
                    }
                }
            } else {
                if ((err = mqtt_disconnect(c))) {
                    printk("Could not disconnect: %d\n", err);
                }
                break;
            }

            /* Send acknowledgment. */
            if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
                const struct mqtt_puback_param ack = {
                    .message_id = p->message_id
                };

                err = mqtt_publish_qos1_ack(c, &ack);
                if (err) {
                    printk("unable to ack\n");
                }
            }
            break;
        }
        case MQTT_EVT_PUBACK:
            if (evt->result != 0) {
                printk("MQTT PUBACK error %d\n", evt->result);
                break;
            }

            printk("[%s:%d] PUBACK packet id: %u\n", __func__, __LINE__,
                   evt->param.puback.message_id);
            break;
        case MQTT_EVT_SUBACK:
            if (evt->result != 0) {
                printk("MQTT SUBACK error %d\n", evt->result);
                break;
            }

            printk("[%s:%d] SUBACK packet id: %u\n", __func__, __LINE__,
                   evt->param.suback.message_id);
            break;
        default:
            printk("[%s:%d] default: %d\n", __func__, __LINE__, evt->type);
            mqttGetResponse();
            break;
    }
}

bool iotex_mqtt_is_connected(void) {
    return connected;
}

const uint8_t *iotex_mqtt_get_client_id() {
    static u8_t client_id_buf[CLIENT_ID_LEN + 1] = { 0 };
#if !defined(CONFIG_CLOUD_CLIENT_ID)
    if (client_id_buf[0] == 0) {
        snprintf(client_id_buf, sizeof(client_id_buf), "%s", iotex_modem_get_imei());
    }
#else
    memcpy(client_id_buf, CONFIG_CLOUD_CLIENT_ID, CLIENT_ID_LEN + 1);
#endif /* !defined(NRF_CLOUD_CLIENT_ID) */
    return client_id_buf;
}

int iotex_mqtt_publish_query(struct mqtt_client *client, enum mqtt_qos qos, char *data, int len)
{
    struct mqtt_publish_param param;
    u8_t pub_topic[MQTT_TOPIC_SIZE];
    iotex_mqtt_query_topic(pub_topic, sizeof(pub_topic));
    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = pub_topic;
    param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
    param.message.payload.data = data;
    param.message.payload.len = len;
    param.message_id = iotex_random(); //sys_rand32_get();
    param.dup_flag = 0U;
    param.retain_flag = 0U;
    printk("publish topic %s \n", pub_topic);
    printk("data: %s \n",data);
    return mqtt_publish(client, &param);
}

int iotex_mqtt_publish_ownership(struct mqtt_client *client, enum mqtt_qos qos, char *data, int len)
{
    struct mqtt_publish_param param;
    u8_t pub_topic[MQTT_TOPIC_SIZE];

    iotex_mqtt_get_ownership_topic(pub_topic, sizeof(pub_topic));
    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = pub_topic;
    param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
    param.message.payload.data = data;
    param.message.payload.len = len;
    param.message_id = iotex_random(); //sys_rand32_get();
    param.dup_flag = 0U;
    param.retain_flag = 0U;
    printk("publish topic %s \n", pub_topic);
    printk("len: %d \n",len);
    return mqtt_publish(client, &param);
}

int iotex_mqtt_publish_data(struct mqtt_client *client, enum mqtt_qos qos, char *data, int len) {
    struct mqtt_publish_param param;
    u8_t pub_topic[MQTT_TOPIC_SIZE];

    iotex_mqtt_get_topic(pub_topic, sizeof(pub_topic));
    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = pub_topic;
    param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
    param.message.payload.data = data;
    param.message.payload.len = len;
    param.message_id = iotex_random(); //sys_rand32_get();
    param.dup_flag = 0U;
    param.retain_flag = 0U;
    return mqtt_publish(client, &param);
}

int iotex_mqtt_heart_beat(struct mqtt_client *client, enum mqtt_qos qos)
{
    struct mqtt_publish_param param;
    u8_t pub_topic[MQTT_TOPIC_SIZE];
    int len;
    uint8_t payload[100];

    printk("iotex_mqtt_heart_beat\n");
    iotex_get_heart_beat_topic(pub_topic, sizeof(pub_topic));
    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = pub_topic;
    param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
    //len = packDevState(payload, sizeof(payload));
    //printk("iotex_mqtt_heart_beat:%s\n",payload);
    printk("iotex_mqtt_heart_beat\n");
    param.message.payload.data = /*payload;*/"a";
    param.message.payload.len = len;
    param.message_id = iotex_random(); //sys_rand32_get();
    param.dup_flag = 0U;
    param.retain_flag = 0U;
    return mqtt_publish(client, &param);
}

int iotex_mqtt_upgrade_over(struct mqtt_client *client, enum mqtt_qos qos)
{    
    struct mqtt_publish_param param;
    u8_t pub_topic[MQTT_TOPIC_SIZE];
    uint8_t payload[100];

    iotex_mqtt_get_upgrdOver_topic(pub_topic, sizeof(pub_topic));
    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = pub_topic;
    param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
    packUpgrdOver(payload, sizeof(payload));
    printk("iotex_mqtt_upgrade_over:%s\n", payload);
    param.message.payload.data = payload;//"a";
    param.message.payload.len = strlen(payload);
    param.message_id = iotex_random(); //sys_rand32_get();
    param.dup_flag = 0U;
    param.retain_flag = 0U;
    return mqtt_publish(client, &param);
}

int iotex_mqtt_configure_upload(struct mqtt_client *client, enum mqtt_qos qos)
{
    struct mqtt_publish_param param;
    u8_t pub_topic[MQTT_TOPIC_SIZE];
    uint8_t payload[300];
    int len;

    iotex_mqtt_get_topic(pub_topic, sizeof(pub_topic));
    printk("configure topic:%s\n", pub_topic);
    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = pub_topic;
    param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
    len = packDevConf(payload, sizeof(payload));
    param.message.payload.data = payload;//"a";
    printk("iotex_mqtt_configure_upload len:%d\n",len);
    param.message.payload.len = len;
    param.message_id = iotex_random(); //sys_rand32_get();
    param.dup_flag = 0U;
    param.retain_flag = 0U;
    return mqtt_publish(client, &param);
}

/**@brief Initialize the MQTT client structure */
int iotex_mqtt_client_init(struct mqtt_client *client, struct pollfd *fds) {
    int err;
    struct sockaddr_storage broker_storage;
    const uint8_t *client_id = iotex_mqtt_get_client_id();

    connected = 0;
    mqtt_client_init(client);

    /* Load mqtt data channel configure */
    iotex_mqtt_load_config();
    broker_init(pmqttBrokerHost, &broker_storage);

    printk("client_id: %s\n", client_id);

    /* MQTT client configuration */
    client->broker = &broker_storage;
    client->evt_cb = mqtt_evt_handler;
    client->client_id.utf8 = client_id;
    client->client_id.size = strlen(client_id);
    client->password = NULL;
    client->user_name = NULL;
    client->protocol_version = MQTT_VERSION_3_1_1;
    client->unacked_ping = 100;

    /* MQTT buffers configuration */
    client->rx_buf = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);
    client->tx_buf = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);

    /* MQTT transport configuration */
    if(CONFIG_MQTT_BROKER_PORT == 1884)
        client->transport.type = MQTT_TRANSPORT_NON_SECURE;
    else
        client->transport.type = MQTT_TRANSPORT_SECURE;

    printk("client->transport.type:%d,CONFIG_MQTT_BROKER_PORT:%d, CONFIG_MQTT_BROKER_HOSTNAME:%s\n", client->transport.type, CONFIG_MQTT_BROKER_PORT,pmqttBrokerHost);

    static sec_tag_t sec_tag_list[] = { CONFIG_CLOUD_CERT_SEC_TAG };
    struct mqtt_sec_config *tls_config = &(client->transport).tls.config;

    tls_config->peer_verify = 2;
    tls_config->cipher_list = NULL;
    tls_config->cipher_count = 0;
    tls_config->sec_tag_count = ARRAY_SIZE(sec_tag_list);
    tls_config->sec_tag_list = sec_tag_list;
    tls_config->hostname = pmqttBrokerHost;

    /* Reboot the device if CONNACK has not arrived in 30s */
    k_delayed_work_init(&cloud_reboot_work, cloud_reboot_work_fn);
    k_delayed_work_submit(&cloud_reboot_work, K_SECONDS(1));
    printk("Before mqtt_connect, connect timeout will reboot in %d seconds\n", CLOUD_CONNACK_WAIT_DURATION);

    if ((err = mqtt_connect(client)) != 0) {
        return err;
    }

    /* Initialize the file descriptor structure used by poll */
    fds->fd = client->transport.tls.sock;
    fds->events = POLLIN;

    return 0;
}
