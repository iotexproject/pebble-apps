
#include <zephyr.h>
#include <sys/util.h>
#include <drivers/gps.h>
#include <modem/lte_lc.h>
#include <drivers/gpio.h>
#include <drivers/uart.h>
#include <stdlib.h>

#include "modem_helper.h"
#include "nvs/local_storage.h"

typedef struct {
    uint8_t boot_ver[20];
    uint8_t hw_ver[20];
    uint8_t sdk_ver[20];
    uint8_t modem_ver[20];
    uint8_t app_ver[20];
} PebbleSYSParam;

static void truncate(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] == 'F') {
            memset(buf + i, 0, len - i);
            break;
        }
    }
}

static int_fast32_t formatSysParam(PebbleSYSParam *psys) {
    uint8_t buf[60];
    uint8_t sysInfo[200] = { 0 };

    uint8_t *pbuf = ReadDataFromModem(PEBBLE_SYS_PARAM_SEC, sysInfo, sizeof(sysInfo));
    if (!pbuf) {
        return 0;
    }

    memcpy(psys, pbuf, sizeof(PebbleSYSParam));
    memset(psys->modem_ver, 0, sizeof(psys->modem_ver));
    getModeVer(buf);
    strcpy(psys->modem_ver, buf);
    truncate(psys->app_ver, sizeof(psys->app_ver));
    truncate(psys->sdk_ver, sizeof(psys->sdk_ver));
    truncate(psys->hw_ver, sizeof(psys->hw_ver));
    truncate(psys->boot_ver, sizeof(psys->boot_ver));
}

void getSysInfor(uint8_t *buf) {
    PebbleSYSParam sysParam;
    formatSysParam(&sysParam);
    memcpy(buf, &sysParam, sizeof(PebbleSYSParam));
}