#include <stdlib.h>
#include <stdio.h>
#include <zephyr.h>
#include <modem/at_cmd.h>
#include <string.h>
#include <logging/log.h>
#include "modem_helper.h"


LOG_MODULE_REGISTER(modem_helper, CONFIG_ASSET_TRACKER_LOG_LEVEL);

atomic_val_t modemWriteProtect = ATOMIC_INIT(0);


static bool isLeap(uint32_t year) {
    return (((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0)));
}

const char *iotex_modem_get_imei(void) {
    int err;
    enum at_cmd_state at_state;
    static char imei_buf[MODEM_IMEI_LEN + 5];

    if ((err = at_cmd_write("AT+CGSN", imei_buf, sizeof(imei_buf), &at_state))) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d \n", err, at_state);
        return "Unknown";
    }

    imei_buf[MODEM_IMEI_LEN] = 0;
    return (const char *)imei_buf;
}

int iotex_model_get_signal_quality(void) {
    enum at_cmd_state at_state;
    char snr_ack[32], snr[4] = { 0 };
    char *p = snr_ack;

    int err = at_cmd_write("AT+CESQ", snr_ack, 32, &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
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
uint32_t iotex_modem_get_local_time(void)
{
    enum at_cmd_state at_state;
    uint32_t localTime;
    uint8_t hh,mm,ss;
    char cclk_r_buf[TIMESTAMP_STR_LEN];

    int err = at_cmd_write("AT+CCLK?", cclk_r_buf, sizeof(cclk_r_buf), &at_state);
    if (err) {
        return  0;
    }
    hh = (cclk_r_buf[17] - '0') * 10 + (cclk_r_buf[18] - '0');
    mm = (cclk_r_buf[20] - '0') * 10 + (cclk_r_buf[21] - '0');
    ss = (cclk_r_buf[23] - '0') * 10 + (cclk_r_buf[24] - '0');
    if(cclk_r_buf[25] == '+') {
        localTime = ((cclk_r_buf[26] - '0') * 10 + (cclk_r_buf[27] - '0'))*15 + mm;
        mm = localTime/60;
        hh += mm;
        mm = localTime - mm*60;
    }
    else {
        localTime = mm + hh * 60 - ((cclk_r_buf[26] - '0') * 10 + (cclk_r_buf[27] - '0'))*15;
        hh = localTime/60;
        mm = localTime - hh * 60;
    }
    localTime = (uint32_t)(hh<<16) |(uint32_t)(mm<<8) |(uint32_t)ss;

    return localTime;
}

const char *iotex_modem_get_clock(iotex_st_timestamp *stamp) {
    double epoch;
    enum at_cmd_state at_state;
    uint32_t YY, MM, DD, hh, mm, ss;

    char cclk_r_buf[TIMESTAMP_STR_LEN];
    static char epoch_buf[TIMESTAMP_STR_LEN];
    int daysPerMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    int err = at_cmd_write("AT+CCLK?", cclk_r_buf, sizeof(cclk_r_buf), &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d \n", err, at_state);
    }
    LOG_DBG("AT CMD Modem time is:%s\n", cclk_r_buf);
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
    LOG_DBG("UTC epoch %s\n", epoch_buf);

    if (stamp) {
        snprintf(stamp->data, sizeof(stamp->data), "%.0f", epoch);
        return stamp->data;
    }
    return (const char *)epoch_buf;
}

double iotex_modem_get_clock_raw(iotex_st_timestamp *stamp) {
    double epoch;
    enum at_cmd_state at_state;
    uint32_t YY, MM, DD, hh, mm, ss;
    char cclk_r_buf[TIMESTAMP_STR_LEN];
    char epoch_buf[TIMESTAMP_STR_LEN];
    int daysPerMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int err = at_cmd_write("AT+CCLK?", cclk_r_buf, sizeof(cclk_r_buf), &at_state);

    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
    }
    LOG_DBG("AT CMD Modem time is:%s\n", cclk_r_buf);
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
    LOG_DBG("UTC epoch %s\n", epoch_buf);
    return epoch;
}

uint32_t iotex_modem_get_battery_voltage(void) {   
    enum at_cmd_state at_state;
    char vbat_ack[32], vbat[5];
    char *p = vbat_ack;

    int err = at_cmd_write("AT%XVBAT", vbat_ack, 32, &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
    }
    p = strchr(p, ':') + 1;
    p++;
    for (err = 0; err < 4; err++) {
        vbat[err] = p[err];
    }
    vbat[4] = 0;
    return (atoi(vbat));
}

void CheckPower(void) {
    volatile float adc_voltage = 0;
    adc_voltage = iotex_modem_get_battery_voltage();
    if (adc_voltage < 3.3) {
        LOG_INF("power lower than 3.3 \n");
        PowerOffIndicator();
    }
}

void dectCard(void) {
    enum at_cmd_state at_state;
    char vbat_ack[32];
    int err = at_cmd_write("AT%XSIM=1", vbat_ack, 32, &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
    }
}

bool cardExist(void) {
    enum at_cmd_state at_state;
    char vbat_ack[32];
    int err;

    memset(vbat_ack, 0, sizeof(vbat_ack));
    err = at_cmd_write("AT%XSIM?", vbat_ack, 32, &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
    }

    return (vbat_ack[7] == '1');
}

bool getModeVer(uint8_t *buf) {
    enum at_cmd_state at_state;
    char vbat_ack[32];
    int err;

    memset(vbat_ack, 0, sizeof(vbat_ack));
    err = at_cmd_write("AT%SHORTSWVER", vbat_ack, 32, &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
        return false;
    }

    sprintf(buf, vbat_ack + 13);
    err = strlen(buf);
    buf[err - 1] = 0;
    buf[err - 2] = 0;
    return true;
}

bool mqttCertExist(void) {
    char *vback = NULL, *p = NULL;
    const char *pkey = "%CMNG:";
    enum at_cmd_state at_state;
    int  i;

    vback = malloc(2048);
    if (!vback) {
        LOG_ERR("line %d space not enough:%d \n", __LINE__, 2048);
        return 0;
    }

    at_cmd_write("AT%CMNG=1,16842753", vback, 2048, &at_state);
    p = vback;
    i = 0;
    while ((p = strstr(p, pkey)) != NULL) {
        i++;
        p += strlen(pkey);
    }
    free(vback);
    return i >= 3;
}

void disableModem(void) {
    at_cmd_write("AT+CFUN=4", NULL, 0, NULL);
}

bool WritDataIntoModem(uint32_t sec, uint8_t *str) {
    enum at_cmd_state at_state;
    int err;
    char vbat_ack[32];
    char *cmd = NULL;
    unsigned int size;

    atomic_set(&modemWriteProtect,1);
    size = strlen(str) + 32;
    cmd = (char *)malloc(size);
    if (!cmd) {
        atomic_set(&modemWriteProtect,0);
        return false;
    }
    at_cmd_write("AT+CFUN=0", vbat_ack, 32, &at_state);
    memset(vbat_ack, 0, sizeof(vbat_ack));
    memset(cmd, 0, size);
    strcpy(cmd, "AT%CMNG=");
    sprintf(cmd + strlen(cmd), "0,%d,0,\"%s\"", sec, str);
    err = at_cmd_write(cmd, vbat_ack, 32, &at_state);
    free(cmd);
    atomic_set(&modemWriteProtect,0);
    return err == 0;
}

uint8_t *ReadDataFromModem(uint32_t sec, uint8_t *buf, uint32_t len) {
    enum at_cmd_state at_state;
    int err;
    char cmd[32];
    uint8_t *pbuf;

    if (len < 79) {
        return NULL;
    }
    strcpy(cmd, "AT%CMNG=");
    sprintf(cmd + strlen(cmd), "2,%d,0", sec);
    err = at_cmd_write(cmd, buf, len, &at_state);
    if (err) {
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

bool isNB(void) {
    enum at_cmd_state at_state;
    char vbat_ack[32];
    int err;

    memset(vbat_ack, 0, sizeof(vbat_ack));
    err = at_cmd_write("AT%XSYSTEMMODE?", vbat_ack, 32, &at_state);
    if (err) {
        LOG_ERR("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
    }

    if(vbat_ack[14] == '1')
        return false;
    else if(vbat_ack[16] == '1')
        return true;

    return false;
}