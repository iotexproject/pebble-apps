#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <power/reboot.h>

#include "display.h"


struct STATUS_BAR {
    uint8_t  sig_icon[20];
    uint8_t  power_icon[20];
    uint8_t  val[MAX_STATUS_ICO];
};

struct STATUS_BAR staBar;

// status bar  ,max size 18 bytes
const uint8_t Signal[]=/*{0x00,0x00,0x20,0x30,0x38,0x3C,0x00,0x00}*/{0x00,0x00,0x00,0x04,0x04,0x00,0x0C,0x0C,0x00,0x1C,0x1C,0x00,0x3C,0x3C,0x00,0x00};
//const uint8_t lte[]={0x00,0x00,0x3E,0x02,0x02,0x00,0x20,0x20,0x3E,0x20,0x20,0x00,0x3E,0x2A,0x2A,0x2A,0x00,0x00};
const uint8_t lte_pic[]={0x3C,0x42,0x81,0x3C,0x42,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x42,0x3C,0x81,0x42,0x3C};
const uint8_t power[]={0x00,0x00,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x18,0x18,0x00,0x00};
const uint8_t gps[] = {0x30,0x78,0xCC,0xC7,0xCC,0x78,0x30};
const uint8_t sim_card[] = {0x00,0x1F,0x21,0x41,0x81,0xBD,0xA5,0xA5,0xA5,0xA5,0xBD,0x81,0x81,0x81,0xFF,0x00};

extern uint8_t s_chDispalyBuffer[128][8];

void sta_LoadIcon(void)
{
    memcpy(staBar.sig_icon, Signal, sizeof(Signal));
    memcpy(staBar.power_icon, power, sizeof(power));
    memset(staBar.val, 0, MAX_STATUS_ICO);
}

void sta_SetMeta(enum E_STATUS_BAR mt, uint8_t val)
{
    staBar.val[mt] = val;
    dectCard();
}

void sta_Refresh(void)
{
    int val;
    uint32_t vol;
    int i, j;
    // signal quality
    //val = iotex_model_get_signal_quality();
    if (!cardExist()) {
        memcpy(staBar.sig_icon, sim_card, sizeof(sim_card));
    } else {
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

    // power , >= 4.1v ful, 4.1 - 3.2 = 0.9
    memcpy(staBar.power_icon, power, sizeof(power));
    vol = iotex_modem_get_battery_voltage();
    val = vol - 3200;
    if (val <= 0) {
        gpio_poweroff();
        k_sleep(K_MSEC(5000));
    }
    val = val > 900 ? 900 : val;
    val /= 90;
    //printk("val:%d\n", val);
    if (val < 9)
        memset(staBar.power_icon+val + 4, 0x42, 9 - val);

    clearDisBuf(6, 7);
    for (i = 0; i < sizeof(Signal); i++) {
        s_chDispalyBuffer[i][7] = staBar.sig_icon[i];
    }

    if (staBar.val[LTE_LINKER]) {
        i += 10;
        for (j = 0; j < sizeof(lte_pic); j++, i++) {
            s_chDispalyBuffer[i][7] = lte_pic[j];
        }
    }

    if (staBar.val[GPS_LINKER]) {
        i += 10;
        for (j = 0; j < sizeof(gps); j++, i++) {
            s_chDispalyBuffer[i][7] = gps[j];
        }
    }
    
    i = 128 - sizeof(power);
    for (j = 0; j < sizeof(power); j++, i++) {
        s_chDispalyBuffer[i][7] = staBar.power_icon[j];
    }
    //ssd1306_refresh_gram();
    ssd1306_refresh_lines(6, 7);
}