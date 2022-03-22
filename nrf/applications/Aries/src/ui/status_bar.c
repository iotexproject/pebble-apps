#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <power/reboot.h>
#include <sys/mutex.h>

#include "display.h"
#include "devReg.h"

extern  struct sys_mutex iotex_hint_mutex;

struct STATUS_BAR {
    uint8_t  sig_icon[20];
    uint8_t  power_icon[20];
    atomic_val_t  val[MAX_STATUS_ICO];
};

struct STATUS_BAR  staBar={
    { 0 },
    { 0 },
    { 0 }
};

struct {
    int sampleDat[16];
    uint32_t index;
} devicePower={
    {0},
    0
};

/*  status bar  ,max size 18 bytes */
const uint8_t Signal[]=/*{0x00,0x00,0x20,0x30,0x38,0x3C,0x00,0x00}*/{0x00,0x00,0x00,0x04,0x04,0x00,0x0C,0x0C,0x00,0x1C,0x1C,0x00,0x3C,0x3C,0x00,0x00};
/* const uint8_t lte[]={0x00,0x00,0x3E,0x02,0x02,0x00,0x20,0x20,0x3E,0x20,0x20,0x00,0x3E,0x2A,0x2A,0x2A,0x00,0x00}; */
const uint8_t lte_pic[]={0x3C,0x42,0x81,0x3C,0x42,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x42,0x3C,0x81,0x42,0x3C};
const uint8_t power[]={0x00,0x00,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x18,0x18,0x00,0x00};
const uint8_t charging[]={0x00,0x00,0x7E,0x7E,0x7E,0x7E,0x76,0x66,0x54,0x32,0x76,0x7E,0x7E,0x7E,0x18,0x18,0x00,0x00};
const uint8_t gps[] = {0x30,0x78,0xCC,0xC7,0xCC,0x78,0x30};
const uint8_t sim_card[] = {0x00,0x1F,0x21,0x41,0x81,0xBD,0xA5,0xA5,0xA5,0xA5,0xBD,0x81,0x81,0x81,0xFF,0x00};
const uint8_t modem_mode_m[]={0x0F,0x08,0x04,0x02,0x04,0x08,0x0F};
const uint8_t modem_mode_n[]={0x0F,0x04,0x02,0x0F};
atomic_val_t  is_modem_sleep;
static struct k_delayed_work   led_flash;

extern uint8_t s_chDispalyBuffer[128][8];
extern void closeGrennLED(void);

void led_flash_fn(struct k_work *work) {
    CtrlBlueLED(false);
}

void ledFlahsWorkerInit(void) {
    k_delayed_work_init(&led_flash, led_flash_fn);
}

void sta_LoadIcon(void) {
    memcpy(staBar.sig_icon, Signal, sizeof(Signal));
    memcpy(staBar.power_icon, power, sizeof(power));
    memset(staBar.val, 0, MAX_STATUS_ICO);
}

void sta_SetMeta(enum E_STATUS_BAR mt, uint8_t val) {
    atomic_set(&staBar.val[mt], val);
}

uint8_t sta_GetMeta(enum E_STATUS_BAR mt) {
    return atomic_get(&staBar.val[mt]);
}

void setModemSleep(atomic_val_t val) {
    atomic_set(&is_modem_sleep, val);
}

bool getModeSleep(void) {
    return  atomic_get(&is_modem_sleep) == (atomic_val_t)1;
}

int getDevVol(void) {
    int j,vol;
    for(vol = 0,j =0; j < sizeof(devicePower.sampleDat)/sizeof(devicePower.sampleDat[0]); j++){
        if(!devicePower.sampleDat[j])
            break;
        vol += devicePower.sampleDat[j];
    }
    if(!j)
        return 0;
    vol /= j;
    return vol;
}

void sta_Refresh(void) {
    int val;
    int vol;
    int i, j;
    static uint8_t index = 0, oledLightTime = 0, ledTime = 0;

    /*  signal quality */
    sys_mutex_lock(&iotex_hint_mutex, K_FOREVER);
    ledTime++;
    if (ledTime > 5) {
        ledTime = 0;
        if (!sta_GetMeta(PEBBLE_POWER)) {
            CtrlBlueLED(true);
            k_delayed_work_submit(&led_flash, K_MSEC(300));
        }
    }

    clearDisBuf(7, 7);
    if (!cardExist()) {
        i = 128 - sizeof(power) - 10 - sizeof(sim_card);
        for (j = 0; j < sizeof(sim_card); j++, i++ ) {
            s_chDispalyBuffer[i][7] = sim_card[j];
        }
    } else {
        if (!getModeSleep()) {
            val = iotex_model_get_signal_quality();
            if (val == 255) val = 0;
            val = val > 27 ? 27 : val;
            val /= 7;
            memcpy(staBar.sig_icon, Signal, sizeof(Signal));
            switch (val) {
                case 0:
                    memset(staBar.sig_icon + 6, 0, sizeof(Signal) - 7);
                    break;
                case 1:
                    memset(staBar.sig_icon + 9, 0, sizeof(Signal) - 10);
                    break;
                case 2:
                    memset(staBar.sig_icon + 12, 0, sizeof(Signal) - 13);
                    break;
                default:
                    break;
            }
        }
    }

    /* power , >= 4.1v ful, 4.1 - 3.2 = 0.9 */
    if (sta_GetMeta(PEBBLE_POWER)) {
        devicePower.sampleDat[devicePower.index++] = 4100;
        devicePower.index &= (sizeof(devicePower.sampleDat)/sizeof(devicePower.sampleDat[0]) - 1);
        memcpy(staBar.power_icon, charging, sizeof(charging));
    } else {
        memcpy(staBar.power_icon, power, sizeof(power));
        devicePower.sampleDat[devicePower.index++] = iotex_hal_adc_sample();
        devicePower.index &= (sizeof(devicePower.sampleDat)/sizeof(devicePower.sampleDat[0]) - 1);
        vol = getDevVol();
        val = vol - 3200;
        if (val <= 0) {
            gpio_poweroff();
            k_sleep(K_MSEC(5000));
        }
        val = val > 900 ? 900 : val;
        val /= 90;
        if (val < 9)
            memset(staBar.power_icon+val + 4, 0x42, 9 - val); 
    }
    if (!sta_GetMeta(LTE_LINKER)) {
        index++;
        if (index > 8)
            index = 0;
        /* memset(staBar.sig_icon, 0, sizeof(staBar.sig_icon)); */
        for(i = 0;i < index * 4; i++) {
            s_chDispalyBuffer[i++][7] = 0x18;
            s_chDispalyBuffer[i++][7] = 0x18;
            s_chDispalyBuffer[i++][7] = 0x0;
            s_chDispalyBuffer[i][7] = 0x0;
        }
        oledLightTime = 0;
    } else {
        for ( i=0; i < sizeof(Signal); i++) {
            s_chDispalyBuffer[i][7] = staBar.sig_icon[i];
        }
        index = 0;
        /* i += 3; */
        /*  N/M is closer to sig_icon */
        for (j = sizeof(Signal) - 1; j > 0; j--, i--) {
            if (staBar.sig_icon[j])
                break;
        }
        i += 1;
        if (isNB()) {
            for (j = 0; j < sizeof(modem_mode_n); j++, i++) {
                s_chDispalyBuffer[i][7] = modem_mode_n[j];
            }
        } else {
            for (j = 0; j < sizeof(modem_mode_m); j++, i++) {
                s_chDispalyBuffer[i][7] = modem_mode_m[j];
            }
        }
        keyWakeup();
        if(!isKeyWakeFlg()) {
            if (iotexReadSteps()) {
                ctrlOLED(true);
                oledLightTime = 0;
                if (devRegGet() == DEV_REG_STOP)
                    dashBoard();
            } else {
                oledLightTime++;
                if (oledLightTime > 10) {
                    oledLightTime = 0;
                    if (devRegGet() == DEV_REG_STOP){
                        ctrlOLED(false);
                    }
                }
            }
        }
    }
    if (sta_GetMeta(AWS_LINKER)) {
        i += 10;
        for (j = 0; j < sizeof(lte_pic); j++, i++) {
            s_chDispalyBuffer[i][7] = lte_pic[j];
        }
    }
    if (sta_GetMeta(GPS_LINKER)) {
        i += 10;
        for (j = 0; j < sizeof(gps); j++, i++) {
            s_chDispalyBuffer[i][7] = gps[j];
        }
    }
    i = 128 - sizeof(power);
    for (j = 0; j < sizeof(power); j++, i++) {
        s_chDispalyBuffer[i][7] = staBar.power_icon[j];
    }
    ssd1306_refresh_lines(7,7);
    sys_mutex_unlock(&iotex_hint_mutex);
}
