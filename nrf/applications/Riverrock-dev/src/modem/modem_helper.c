#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <nrf_modem_at.h>
#include "modem_helper.h"


LOG_MODULE_REGISTER(modem_helper, CONFIG_ASSET_TRACKER_LOG_LEVEL);

atomic_val_t modemWriteProtect = ATOMIC_INIT(0);


static bool isLeap(uint32_t year) {
    return (((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0)));
}

const char *iotex_modem_get_imei(void) {
    int err;
    static char imei_buf[MODEM_IMEI_LEN + 50];

    if ((err = nrf_modem_at_cmd(imei_buf, sizeof(imei_buf), "AT+CGSN"))) {
        LOG_ERR("iotex_modem_get_imei: %d \n", err);
        return "Unknown";
    }
    imei_buf[MODEM_IMEI_LEN] = 0;
    return (const char *)imei_buf;
}

int iotex_model_get_signal_quality(void) {
    char snr_ack[100], snr[4] = { 0 };
    char *p = snr_ack;

    int err = nrf_modem_at_cmd(snr_ack, sizeof(snr_ack), "AT+CESQ");
    if (err) {
        LOG_ERR("iotex_model_get_signal_quality: %d", err);
        return  0;
    }
    p = strchr(p, ',') + 1;
    p = strchr(p, ',') + 1;
    p = strchr(p, ',') + 1;
    p = strchr(p, ',') + 1;
    int i = 0;
    while (*p != ',') {
        snr[i++] = *p++;
    }
    return atoi(snr);
}

const char *iotex_modem_get_clock(iotex_st_timestamp *stamp) {
    double epoch;
    uint32_t YY, MM, DD, hh, mm, ss;

    char cclk_r_buf[TIMESTAMP_STR_LEN];
    static char epoch_buf[TIMESTAMP_STR_LEN];
    int daysPerMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    int err = nrf_modem_at_cmd(cclk_r_buf, sizeof(cclk_r_buf), "AT+CCLK?");
    if (err) {
        printf("iotex_modem_get_clock: %d \n", nrf_modem_at_err(err));
        return NULL;
    }
    LOG_INF("AT CMD Modem time is:%s\n", cclk_r_buf);
    /*  num of years since 1900, the formula works only for 2xxx */
    YY = (cclk_r_buf[8] - '0') * 10 + (cclk_r_buf[9] - '0') + 100;
    MM = (cclk_r_buf[11] - '0') * 10 + (cclk_r_buf[12] - '0');
    DD = (cclk_r_buf[14] - '0') * 10 + (cclk_r_buf[15] - '0');
    hh = (cclk_r_buf[17] - '0') * 10 + (cclk_r_buf[18] - '0');
    mm = (cclk_r_buf[20] - '0') * 10 + (cclk_r_buf[21] - '0');
    ss = (cclk_r_buf[23] - '0') * 10 + (cclk_r_buf[24] - '0');
    if (isLeap(YY + 1900)) {
        daysPerMonth[2] = 29;
    }
    /*  accumulate */
    for (int i = 1; i <= 12; i++) {
        daysPerMonth[i] += daysPerMonth[i - 1];
    }

    epoch = ss + mm * 60 + hh * 3600 + (daysPerMonth[ MM - 1] + DD - 1) * 86400 +
             (YY - 70) * 31536000L + ((YY - 69) / 4) * 86400L -
             ((YY - 1) / 100) * 86400L + ((YY + 299) / 400) * 86400L;

    snprintf(epoch_buf, sizeof(epoch_buf), "%.0f", epoch);
    LOG_INF("UTC epoch %s\n", epoch_buf);

    if (stamp) {
        snprintf(stamp->data, sizeof(stamp->data), "%.0f", epoch);
        return stamp->data;
    }
    return (const char *)epoch_buf;
}

double iotex_modem_get_clock_raw(iotex_st_timestamp *stamp) {
    double epoch;

    uint32_t YY, MM, DD, hh, mm, ss;
    char cclk_r_buf[TIMESTAMP_STR_LEN];
    char epoch_buf[TIMESTAMP_STR_LEN];
    int daysPerMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int err = nrf_modem_at_cmd(cclk_r_buf, sizeof(cclk_r_buf), "AT+CCLK?");

    if (err) {
        LOG_ERR("iotex_modem_get_clock_raw: %d", err);
    }
    LOG_INF("AT CMD Modem time is:%s\n", cclk_r_buf);
    /*  num of years since 1900, the formula works only for 2xxx */
    YY = (cclk_r_buf[8] - '0') * 10 + (cclk_r_buf[9] - '0') + 100;
    MM = (cclk_r_buf[11] - '0') * 10 + (cclk_r_buf[12] - '0');
    DD = (cclk_r_buf[14] - '0') * 10 + (cclk_r_buf[15] - '0');
    hh = (cclk_r_buf[17] - '0') * 10 + (cclk_r_buf[18] - '0');
    mm = (cclk_r_buf[20] - '0') * 10 + (cclk_r_buf[21] - '0');
    ss = (cclk_r_buf[23] - '0') * 10 + (cclk_r_buf[24] - '0');
    if (isLeap(YY + 1900)) {
        daysPerMonth[2] = 29;
    }
    /*  accumulate */
    for (int i = 1; i <= 12; i++) {
        daysPerMonth[i] += daysPerMonth[i - 1];
    }

    epoch  = ss + mm * 60 + hh * 3600 + (daysPerMonth[ MM - 1] + DD - 1) * 86400 +
             (YY - 70) * 31536000L + ((YY - 69) / 4) * 86400L -
             ((YY - 1) / 100) * 86400L + ((YY + 299) / 400) * 86400L;

    snprintf(epoch_buf, sizeof(epoch_buf), "%.0f", epoch);
    LOG_INF("UTC epoch %s\n", epoch_buf);
    return epoch;
}

uint32_t iotex_modem_get_battery_voltage(void) {   

    char vbat_ack[32], vbat[5];
    char *p = vbat_ack;
    int err = nrf_modem_at_cmd(vbat_ack, 32, "AT%%XVBAT");
    if (err) {
        LOG_ERR("iotex_modem_get_battery_voltage: %d", err);
        return 0;
    }
    p = strchr(p, ':') + 1;
    p++;
    for (err = 0; err < 4; err++) {
        vbat[err] = p[err];
    }
    vbat[4] = 0;
    return (atoi(vbat));
}
/* power off pebble tracker if voltage is below 3.3v */
void CheckPower(void) {
    volatile float adc_voltage = 0;
    adc_voltage = iotex_modem_get_battery_voltage();
    if (adc_voltage < 3.3) {
        LOG_INF("power lower than 3.3 \n");
        PowerOffIndicator();
    }
}

/* Is the sim card inserted */
bool cardExist(void) {

    char vbat_ack[32];
    int err;

    memset(vbat_ack, 0, sizeof(vbat_ack));
    err = nrf_modem_at_cmd(vbat_ack, 32, "AT%%XSIM?");
    if (err) {
        LOG_ERR("cardExist: %d", err);
        return  false;
    }

    return (vbat_ack[7] == '1');
}
/* Read modem firmware version*/
bool getModeVer(uint8_t *buf) {

    char vbat_ack[32];
    int err;

    memset(vbat_ack, 0, sizeof(vbat_ack));
    err = nrf_modem_at_cmd(vbat_ack, 32, "AT%%SHORTSWVER");
    if (err) {
        LOG_ERR("getModeVer: %d", err);
        return false;
    }

    sprintf(buf, vbat_ack + 13);
    err = strlen(buf);
    buf[err - 1] = 0;
    buf[err - 2] = 0;
    return true;
}
/* Check for sufficient certificates */
bool mqttCertExist(void) {
    char *vback = NULL, *p = NULL;
    const char *pkey = "%CMNG:";

    int  i;

    vback = malloc(2048);
    if (!vback) {
        LOG_ERR("line %d space not enough:%d \n", __LINE__, 2048);
        return 0;
    }

    nrf_modem_at_cmd(vback, 2048, "AT%%CMNG=1,16842753");
    p = vback;
    i = 0;
    while ((p = strstr(p, pkey)) != NULL) {
        i++;
        p += strlen(pkey);
    }
    free(vback);
    return i >= 3;
}
/* Modem shutdown */
void disableModem(void) {
    nrf_modem_at_cmd(NULL, 0,"AT+CFUN=4");
}
/* Wrtie data into modem */
bool WritDataIntoModem(uint32_t sec, uint8_t *str) {

    int err;
    char vbat_ack[100];
    char *cmd = NULL;
    unsigned int size;

    atomic_set(&modemWriteProtect,1);
    size = strlen(str) + 32;
    cmd = (char *)malloc(size);
    if (!cmd) {
        atomic_set(&modemWriteProtect,0);
        LOG_ERR("WritDataIntoModem not enough space\n" );
        return false;
    }
    nrf_modem_at_cmd(vbat_ack, sizeof(vbat_ack), "AT+CFUN=0");
    memset(vbat_ack, 0, sizeof(vbat_ack));
    memset(cmd, 0, size);
    strcpy(cmd, "AT%%CMNG=");
    sprintf(cmd + strlen(cmd), "0,%d,0,\"%s\"", sec, str);
    err = nrf_modem_at_cmd(vbat_ack, sizeof(vbat_ack), cmd);
    free(cmd);
    atomic_set(&modemWriteProtect,0);
    return err == 0;
}
/* Read data from modem flash*/
uint8_t *ReadDataFromModem(uint32_t sec, uint8_t *buf, uint32_t len) {
    int err;
    char cmd[32];
    uint8_t *pbuf;

    if (len < 79) {
        return NULL;
    }
    strcpy(cmd, "AT%%CMNG=");
    sprintf(cmd + strlen(cmd), "2,%d,0", sec);
    err = nrf_modem_at_cmd(buf, len, cmd);
    if (err) {
        LOG_ERR("ReadDataFromModem error:%d\n", err);
        return NULL;
    } else {
        pbuf = strchr(buf, ',');
        pbuf++;
        pbuf = strchr(pbuf, ',');
        pbuf++;
        pbuf = strchr(pbuf, ',');
        pbuf += 2;
        return pbuf;
    }
}
/*Return the modem current model is NB-IOT or not*/
bool isNB(void) {
    char vbat_ack[32];
    int err;

    memset(vbat_ack, 0, sizeof(vbat_ack));
    err = nrf_modem_at_cmd(vbat_ack, sizeof(vbat_ack), "AT%%XSYSTEMMODE?");
    if (err) {
        LOG_ERR("isNB: %d", err);
        return false;
    }

    if(vbat_ack[14] == '1')
        return false;
    else if(vbat_ack[16] == '1')
        return true;

    return false;
}