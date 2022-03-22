#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <net/cloud.h>
#include <net/socket.h>
#include <net/nrf_cloud.h>
#include <logging/log.h>
#include "devReg.h"
#include "ui.h"
#include "cJSON.h"
#include "cJSON_os.h"
#include "ecdsa.h"
#include "mqtt/mqtt.h"
#include "display.h"
#include "watchdog.h"
#include "config.h"
#include "ver.h"

#include "pb_decode.h"
#include "pb_encode.h"
#include "package.pb.h"

LOG_MODULE_REGISTER(devReg, CONFIG_ASSET_TRACKER_LOG_LEVEL);

static uint32_t devRegStatus = DEV_REG_STOP;
static uint8_t walletAddr[200];

extern void startOTA(uint8_t *url);
extern int hexStr2Bin(char *str, char *bin);

bool IsDevReg(void) {
    return (devRegStatus >= DEV_REG_START);
}

void devRegSet(enum DEV_REG_STA esta) {
    devRegStatus = esta;
}

uint32_t devRegGet(void) {
    return devRegStatus;
}

int iotex_mqtt_get_wallet(const uint8_t *payload, uint32_t len) {
    int ret = 0;

    cJSON *walletAddress = NULL;
    cJSON *regStatus = NULL;
    cJSON *root_obj = cJSON_Parse(payload);
    cJSON *Uri = NULL;
    cJSON *app = NULL;
    cJSON *ver = NULL;

    if (!root_obj) {
        const char *err_ptr = cJSON_GetErrorPtr();
        if (err_ptr) {
            LOG_ERR("[%s:%d] error before: %s\n", __func__, __LINE__, err_ptr);
        }
        return 0;
    }

    regStatus = cJSON_GetObjectItem(root_obj, "status");
    if (!regStatus) {
        LOG_ERR("Poll proposal error\n");
        ret = 0;
        goto cleanup;
    }

    if (regStatus->valueint == 1) {
        walletAddress = cJSON_GetObjectItem(root_obj, "proposer");
        if (walletAddress) {
            LOG_INF("proposer: %s \n", walletAddress->valuestring);
            strcpy(walletAddr, walletAddress->valuestring);
            ret = 1;
        }
    } else if (regStatus->valueint == 2) {
        ret = 2;
    }
    clrfirmwareUrl();
    app = cJSON_GetObjectItem(root_obj, "firmware");
    ver = cJSON_GetObjectItem(root_obj, "version");
    if (app && cJSON_IsString(app) && !strcmp(app->valuestring, IOTEX_APP_NAME)) {
        if (ver && cJSON_IsString(ver) && (compareVersion(ver->valuestring, RELEASE_VERSION)>0)) {
            Uri = cJSON_GetObjectItem(root_obj, "uri");
            if (Uri && cJSON_IsString(Uri)) {
                /* strcpy(url, firmwareUri->valuestring); */
                strcpy(getOTAUrl(),  Uri->valuestring);
                LOG_INF("firmwareUrl:%s \n", getOTAUrl());
            }
        }
    }

cleanup:
    cJSON_Delete(root_obj);
    return ret;
}

int SignAndSend(void)
{
    char esdaSign[65];
    char jsStr[130];
    char *json_buf = NULL;
    int sinLen;
    uint32_t uint_timestamp;

    ConfirmPackage confirmAdd = ConfirmPackage_init_zero;
    uint_timestamp = atoi(iotex_modem_get_clock(NULL));
    json_buf = malloc(DATA_BUFFER_SIZE);
    if (!json_buf)
        return -1;

    LOG_INF("walletAddr:%s\n", walletAddr);
    confirmAdd.owner.size = hexStr2Bin(walletAddr + 2, confirmAdd.owner.bytes);
    confirmAdd.owner.bytes[confirmAdd.owner.size] = (char)((uint_timestamp & 0xFF000000) >> 24);
    confirmAdd.owner.bytes[confirmAdd.owner.size + 1] = (char)((uint_timestamp & 0x00FF0000) >> 16);
    confirmAdd.owner.bytes[confirmAdd.owner.size + 2] = (char)((uint_timestamp & 0x0000FF00) >> 8);
    confirmAdd.owner.bytes[confirmAdd.owner.size + 3] = (char)(uint_timestamp & 0x000000FF);
    doESDASign(confirmAdd.owner.bytes, confirmAdd.owner.size + 4, esdaSign, &sinLen);
    memcpy(confirmAdd.signature, esdaSign, 64);
    confirmAdd.timestamp = uint_timestamp;
    confirmAdd.channel = getDevChannel();
    pb_ostream_t enc_packstream;
    enc_packstream = pb_ostream_from_buffer(json_buf, DATA_BUFFER_SIZE);
    if (!pb_encode(&enc_packstream, ConfirmPackage_fields, &confirmAdd)) {
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_packstream));
        free(json_buf);
        return -2;
    }
    publish_dev_ownership(json_buf, enc_packstream.bytes_written);
    free(json_buf);
    return 0;
}

void waitForOtaOver(void) {
    uint8_t disOTAProgress[30];
    dis_OnelineText(1, ALIGN_LEFT, " ",DIS_NORMAL);
    dis_OnelineText(2, ALIGN_LEFT, " ",DIS_NORMAL);
    dis_OnelineText(3, ALIGN_LEFT, " ",DIS_NORMAL);
    strcpy(disOTAProgress, "Downloaded: ");
    while (devRegGet() != DEV_UPGRADE_COMPLETE) {
        k_sleep(K_MSEC(300));
        sprintf(disOTAProgress+12, "%d", getOTAProgress());
        strcpy(disOTAProgress+strlen(disOTAProgress), "%");
        dis_OnelineText(2, ALIGN_LEFT, disOTAProgress,DIS_NORMAL);
    }
}

void stopTaskBeforeOTA(void) {
    stopWatchdog();
    stopMqtt();
    stopAnimationWork();
}

void mainStatus(struct mqtt_client *client) {
    switch (devRegGet()) {
        case DEV_REG_STATUS:
            publish_dev_query("", 0);
            break;
        case DEV_REG_PRESS_ENTER:
            if (IsEnterPressed()) {
                ClearKey();
                devRegSet(DEV_REG_POLL_FOR_WALLET);
                hintString(htRegRequest,HINT_TIME_FOREVER); 
            }
            break;
        case DEV_REG_POLL_FOR_WALLET:
            publish_dev_query("", 0);
            break;
        case DEV_REG_ADDR_CHECK:
            sprintf(htRegaddrChk_en,"%s", walletAddr);
            hintString(htRegaddrChk,HINT_TIME_FOREVER);
            if (IsEnterPressed()) {
                devRegSet(DEV_REG_SIGN_SEND);
                hintString(htRegWaitACK,HINT_TIME_FOREVER);
            }
            break;
        case DEV_REG_SIGN_SEND:
            SignAndSend();
            devRegSet(DEV_REG_POLL_STATE);
            break;
        case DEV_REG_POLL_STATE:
            publish_dev_query("", 0);
            /* devRegSet(DEV_REG_SUCCESS); */
            break;
        case DEV_REG_SUCCESS:
            hintString(htRegSuccess, HINT_TIME_DEFAULT);
            k_sleep(K_SECONDS(3));
            ClearKey();
            devRegSet(DEV_REG_STOP);
            break;
        case DEV_UPGRADE_ENTRY:
            hintString(htConnecting, HINT_TIME_FOREVER);
            break;
        case DEV_UPGRADE_CONFIRM:
            publish_dev_query("", 0);
            break;
        case DEV_UPGRADE_STARTED:
            stopTaskBeforeOTA();
            startOTA(getOTAUrl());
            devRegSet(DEV_REG_STOP);
            LOG_INF("waiting for upgrade\n");
            waitForOtaOver();
            LOG_INF("upgrade over, system reboot\n");
            k_sleep(K_MSEC(500));
            sys_reboot(0);
            break;
        case DEV_REG_STOP:
            break;
        default:
            break;
    }
}

int iotexDevBinding(struct pollfd *fds, struct mqtt_client *client) {
    int err = 0;
    int tmCount = 0;

    while (true) {
        err = poll(fds, 1, CONFIG_MAIN_BASE_TIME);
        if (err < 0) {
            LOG_ERR("ERROR: poll %d\n", errno);
            break;
        }
        if ((fds->revents & POLLIN) == POLLIN) {
            err = mqtt_input(client);
            if (err != 0) {
                LOG_ERR("ERROR: mqtt_input %d\n", err);                
                break;
            }
        }
        if ((fds->revents & POLLERR) == POLLERR) {
            LOG_ERR("POLLERR\n");
            err = -1;
            break;
        }
        if ((fds->revents & POLLNVAL) == POLLNVAL) {
            LOG_ERR("POLLNVAL\n");
            err = -2;
            break;
        }
        err = mqtt_live(client);
        if (err != 0 && err != -EAGAIN) {
            LOG_ERR("ERROR: mqtt_live %d\n", err);
            break;
        }
        mainStatus(client);
        if (!hintTimeDec()) {
            pebbleBackGround(0);
        }
        if(devRegGet() == DEV_REG_STOP) {
            clrHint();
            err = (err == -EAGAIN ? 0 : err);
            break;
        } else {
            /*  check  registration  status every 1s */
            k_sleep(K_MSEC(5000));
        }
    }
    return err;
}
