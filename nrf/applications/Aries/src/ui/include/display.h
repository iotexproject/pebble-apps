

#ifndef __DISPLAY_H__
#define  __DISPLAY_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

enum E_STATUS_BAR{
    STRENGTH,
    LTE_LINKER,
    GPS_LINKER,
    PEBBLE_POWER,
    AWS_LINKER,
    MAX_STATUS_ICO
};

#define  STA_LINKER_ON      1
#define  STA_LINKER_OFF     0
#define  STATUS_BAR_HIGHT   8

#define  HINT_TIME_FOREVER       0
#define  HINT_TIME_DEFAULT       4  /*  4 x 5s = 20s */

/*  refresh  cycle  ms */
#define STA_REFRESH_T       (30*1000)   

#define    HT_LANGUAGE_EN   0  
#define    HT_LANGUAGE_CN   1
#define    HT_LANG_CN_SIZE  2


/*  display left align or  centralized */
#define   ALIGN_CENTRALIZED     0
#define   ALIGN_LEFT            1

/*  display revert ? */
#define  DIS_NORMAL             1
#define  DIS_REVERT             0
#define  DIS_CURSOR(a, b)       ((a) == (b) ? DIS_REVERT:DIS_NORMAL)


/*  hints */
extern const uint8_t *htNetConnected[];
extern const uint8_t *htRegRequest[];
extern const uint8_t *htRegWaitACK[];
extern const uint8_t *htRegSuccess[];
extern const uint8_t *htconfirmUpgrade[];
extern const uint8_t *htUpgrading[];
extern const uint8_t *htRegaddrChk[];
extern const uint8_t *htConnecting[];
extern const uint8_t *htSelectApp[];
extern const uint8_t *htstartReconf[];
extern const uint8_t *htstartMqtt[];
extern const uint8_t *htupdateConfig[];
extern const uint8_t *httpNoAppUpgrd[];
extern uint8_t  htRegaddrChk_en[];
extern  uint8_t htLanguage;
extern const uint8_t pic_pedometerSteps[];
extern const uint8_t pic_pedometerTime[];
extern const uint8_t pic_pedometerCal[];


void ssd1306_init(void);
void clearDisBuf(uint8_t start_line, uint8_t stop_line);
void ssd1306_refresh_lines(uint8_t start_line, uint8_t stop_line);
void sta_SetMeta(enum E_STATUS_BAR mt, uint8_t val);
void sta_Refresh(void);
void hintInit(void);
uint8_t hintTimeDec(void);
void  hintString(uint8_t *str[], uint8_t tim);
void MainMenu(void);
void dashBoard(void);
void ctrlOLED(bool on_off);
void pebbleBackGround(uint32_t sel);
int getDevVol(void);
void dis_iconString(uint8_t *icon, uint32_t line, uint8_t *str);



#ifdef __cplusplus
}
#endif

#endif