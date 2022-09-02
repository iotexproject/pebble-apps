#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <logging/log.h>
#include <power/reboot.h>
#include <sys/mutex.h>
#include <modem/lte_lc.h>
#include "hints_data.h"
#include "display.h"
#include "ver.h"
#include "keyBoard.h"
#include "nvs/local_storage.h"
#include "devReg.h"

LOG_MODULE_REGISTER(hint, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define HINT_WIDTH   128
#define HINT_HEIGHT  56
#define HINT_XPOS_START  0
#define HINT_YPOS_START  16
#define HINT_FONT   16
#define HINT_MAX_LINE   (HINT_HEIGHT/HINT_FONT)

#define HINT_CENTRAL_LINE   24

#define  PEBBLE_MODEM_NB_IOT        0
#define  PEBBLE_MODEM_LTE_M         1
#define  PEBBLE_IOTEX_MAIN_NET   0
#define  PEBBLE_IOTEX_TEST_NET   1
#define  PEBBLE_USER_MAIN_NET    2
#define  PEBBLE_USER_TEST_NET    3


/* There are not enough flash, so defined in RAM */
static uint8_t pebble_net_type[] =
{
    PEBBLE_IOTEX_MAIN_NET,
    PEBBLE_IOTEX_TEST_NET,
    PEBBLE_USER_MAIN_NET,
    PEBBLE_USER_TEST_NET
};

static uint8_t hintAliveTime;
uint8_t htLanguage;
struct sys_mutex iotex_hint_mutex;
static uint32_t pubCounts = 0;
const uint8_t textLine[] = { 0, 16, 32, 48 };
static uint8_t pebbleModem = PEBBLE_MODEM_NB_IOT;
static uint8_t pebbleContractNet = PEBBLE_IOTEX_MAIN_NET;


extern uint8_t s_chDispalyBuffer[128][8];
extern  const  uint8_t *pmqttBrokerHost;
extern  uint16_t mqtt_port;
extern  void ledFlahsWorkerInit(void);
extern  void useDefaultHost(void);
extern  void saveNet(uint8_t *host, uint8_t *port, uint8_t net);
extern  void getEndpointFromConf(void);
extern bool startConf(void);
extern void  uploadSysInfo(void);

const uint8_t AreaSec[][2]={
    {ASIA_PACIFIC_CERT_SEC, ASIA_PACIFIC_KEY_SEC},
    {EUROPE_FRANKFURT_CERT_SEC, EUROPE_FRANKFURT_KEY_SEC},
    {MIDDLE_EAST_CERT_SEC, MIDDLE_EAST_KEY_SEC},
    {NORTH_AMERICA_CERT_SEC, NORTH_AMERICA_KEY_SEC},
    {SOUTH_AMERICA_CERT_SEC, SOUTH_AMERICA_KEY_SEC},
    {USER_MAIN_CERT_SEC, USER_MAIN_KEY_SEC},
    {USER_TEST_CERT_SEC, USER_TEST_KEY_SEC},
};

const uint8_t *mqttBrokerHost[5]={
    "a11homvea4zo8t-ats.iot.ap-east-1.amazonaws.com",
    "a11homvea4zo8t-ats.iot.eu-central-1.amazonaws.com",
    "a11homvea4zo8t-ats.iot.me-south-1.amazonaws.com",
    "a11homvea4zo8t-ats.iot.us-east-1.amazonaws.com",
    "a11homvea4zo8t-ats.iot.sa-east-1.amazonaws.com"
};

void pubOnePack(void) {
    pubCounts++;
}

void hintInit(void) {
    hintAliveTime = 0;
    htLanguage = HT_LANGUAGE_EN;
    sys_mutex_init(&iotex_hint_mutex);
    ledFlahsWorkerInit();
}

uint8_t hintTimeDec(void) {
    if (hintAliveTime > 0) {
        hintAliveTime--;
        if (!hintAliveTime)
            return 0;
    } else {
        return 1;
    }
    return hintAliveTime;
}

void clrHint(void) {
    if (hintAliveTime) {
        hintAliveTime = 0;
    }
}

void hintString(uint8_t *str[], uint8_t tim) {
    int len, lines, i, j, k;
    uint8_t xpos, ypos;
    uint8_t *dis;

    sys_mutex_lock(&iotex_hint_mutex, K_FOREVER);
    hintAliveTime = tim;
    dis = str[htLanguage];
    clearDisBuf(0,5);

    if (htLanguage == HT_LANGUAGE_EN) {
        len = strlen(dis);
        if (len > 16) {
            lines = ((len << 3) + HINT_WIDTH - 1) / HINT_WIDTH;
            if (lines <= HINT_MAX_LINE) {
                xpos = 0;
                ypos = HINT_FONT;
            } else {
                LOG_ERR("Hints too long:%s, lines:%d\n", dis, lines);
                sys_mutex_unlock(&iotex_hint_mutex);
                return;
            }
        } else {
            ypos = HINT_HEIGHT / 2;
            xpos = (HINT_WIDTH - (len << 3)) / 2;
        }
        ssd1306_display_string(xpos, ypos, dis, 16, 1);
    } else {
        len = str[HT_LANG_CN_SIZE];
        if (len > 256) {
            lines = len / 256;
            if (lines <= HINT_MAX_LINE) {
                xpos = 0;
                ypos = HINT_FONT;
            } else {
                LOG_ERR("Chinese Hints too long\n");
                sys_mutex_unlock(&iotex_hint_mutex);
                return;
            }
        } else {
            /* ypos = (HINT_HEIGHT+2)/2; */
            ypos = HINT_HEIGHT / 2;
            xpos = (HINT_WIDTH - (len >> 1)) / 2;
        }
        ypos = 7 - ypos / 8;
        LOG_DBG("xpos:%d, ypos:%d, len:%d\n", xpos,ypos,len);
        for (j = 0; j < len; ypos -= 2) {
            k = (len - j) >= 256 ? 128 : ((len - j) / 2 + xpos);
            for (i = xpos; i < k; i++, j++) {
                s_chDispalyBuffer[i][ypos] = dis[j];
                s_chDispalyBuffer[i][ypos-1] = dis[j + (k - xpos)];
            }
            j += (k - xpos);
            ypos--;
        }
    }

    ssd1306_refresh_lines(0,5);
    sys_mutex_unlock(&iotex_hint_mutex);
}

/*
    all 4 lines  4 x 16 = 64
    line : one line  of 4 lines
    flg : centered or  left  align
    str : display text
*/
void dis_OnelineText(uint32_t line, uint32_t flg, uint8_t *str, uint8_t revert)
{
    uint8_t xpos, ypos;
    uint32_t len = strlen(str);

    clearDisBuf(6 - 2 * line, 7 - 2 * line);
    if (len > 16) {
        LOG_ERR("Hints too long:%s\n", str);
        return;
    } else {
        /*  left align */
        if (flg) {
            xpos = 0;
        } else {
            xpos = (HINT_WIDTH - (len << 3)) / 2;
        }
        ypos = textLine[line];
    }
    ssd1306_display_string(xpos, ypos, str, 16, revert);
    /* ssd1306_refresh_lines(line*2,line*2+1); */
    ssd1306_refresh_lines(6-2*line,7-2*line);
}

void dashBoard(void) {
    uint8_t disBuf[20];

    /* ssd1306_clear_screen(0); */
    /*  app */
    strcpy(disBuf, IOTEX_APP_NAME);
    dis_OnelineText(1, ALIGN_CENTRALIZED, disBuf,DIS_NORMAL);
    strcpy(disBuf, "v"RELEASE_VERSION);
    dis_OnelineText(2, ALIGN_CENTRALIZED, disBuf,DIS_NORMAL);
    /*  packages */
    strcpy(disBuf, "Package Sent:");
    sprintf(disBuf + strlen(disBuf), "%d", pubCounts);
    dis_OnelineText(3, ALIGN_LEFT, disBuf,DIS_NORMAL);    
}

void  fatalError(void) {
    ssd1306_clear_screen(0);
    dis_OnelineText(1, ALIGN_CENTRALIZED, "Fatal Error", DIS_NORMAL); 
    dis_OnelineText(3, ALIGN_CENTRALIZED, "Shut down ...", DIS_NORMAL); 
    k_sleep(K_SECONDS(10));
    gpio_poweroff();
}

bool uploadInfo(void) {
    unsigned int i;
    uploadSysInfo();
    k_sleep(K_MSEC(600));
    return startConf();
}

/*  startup menu */
bool checkMenuEntry(void)
{
    return (isDownKeyStartupPressed() || uploadInfo());
}
static int regionIndex(int id)
{
    int i;
    uint8_t allArea[][20] = {
        "MAINIOTX        ",
        "Europe          ",
        "Middle East     ",
        "TESTIOTX        ",
        "South America   ",
        "MAINUSER        ",
        "TESTUSER        ",
    };
    uint8_t showArea[][20] = {
        "MAINIOTX        ",
        "TESTIOTX        ",
        "MAINUSER        ",
        "TESTUSER        ",
        "Exit            "
    };
    uint8_t *region = showArea[id];

    for(i = 0; i < (sizeof(allArea) / sizeof(allArea[0])); i++) {
        if(!strcmp(region, allArea[i])) {
            printk("region: %s \n", region);
            printk("i:%d\n", i);
            return i;
        }
    }
    return  0;
}

bool updateCert(int id) {
    uint8_t *pcert = NULL;
    uint8_t *pkey = NULL;
    uint8_t *proot = NULL;
    uint8_t *pbuf_cert = NULL;
    uint8_t *pbuf_key = NULL;
    uint8_t *pbuf_root = NULL;
    uint8_t index[5];
    int selArea = regionIndex(id);

    pcert = malloc(2048);
    pkey = malloc(2048);
    proot = malloc(2048);
    if (!pcert || !pkey || !proot) {
        LOG_ERR("mem not enough !\n");
        if (pcert)
            free(pcert);
        if (pkey)
            free(pkey);
        if (proot)
            free(proot);
        return false;
    }

    pbuf_cert = ReadDataFromModem(AreaSec[selArea][0], pcert, 2048);
    pbuf_key = ReadDataFromModem(AreaSec[selArea][1], pkey, 2048);
    if(selArea <= 4) {
        pbuf_root = ReadDataFromModem(AWS_ROOT_CA, proot, 2048);
    }
    else {
        pbuf_root = ReadDataFromModem(AreaSec[selArea][0] + 1, proot, 2048);
    }
    if (!pbuf_cert || !pbuf_key || !pbuf_root) {
        LOG_ERR("read cert error \n");
        goto out;
    }
    pbuf_cert[strlen(pbuf_cert) - 3] = 0;
    pbuf_key[strlen(pbuf_key) - 3] = 0;
    pbuf_root[strlen(pbuf_root) - 3] = 0;
    WriteCertIntoModem(pbuf_cert, pbuf_key, pbuf_root);
    if(selArea <= 4) {
        pmqttBrokerHost = mqttBrokerHost[selArea];
        mqtt_port = 8883;
        saveNet(pmqttBrokerHost, "8883", pebble_net_type[id]);
    }else {
        /* Read endpoint */
        pbuf_root = ReadDataFromModem(AreaSec[selArea][0] + 2, proot, 2048);
        pbuf_root[strlen(pbuf_root) - 3] = 0;
        /* Read port */
        pbuf_cert = ReadDataFromModem(AreaSec[selArea][0] + 3, pcert, 2048);
        pbuf_cert[strlen(pbuf_cert) - 3] = 0;
        if (!pbuf_cert || !pbuf_root) {
            LOG_ERR("read endpoint error \n");
            goto out;
        }
        saveNet(pbuf_root, pbuf_cert, pebble_net_type[id]);
    }
    pebbleContractNet = pebble_net_type[id];
    itoa(id, index, 10);
    index[1] = 0;
    WritDataIntoModem(MQTT_CERT_INDEX, index);
    return true;
out:
    free(pcert);
    free(pkey);
    free(proot);
    return false;
}

void initBrokeHost(void) {
    uint8_t buf[100];
    uint8_t *pbuf;
    int selArea;

    pbuf = ReadDataFromModem(MQTT_CERT_INDEX, buf, sizeof(buf));
    if (pbuf != NULL) {
        pbuf[1] = 0;
        selArea = atoi(pbuf);
        pebbleContractNet = pebble_net_type[selArea];
        getEndpointFromConf();
    }

    buf[0] = 0xFF;
    iotex_local_storage_load(SID_MODEM_WORK_MODE, buf, 1);
    if((buf[0] != 0) && (buf[0] != 1)) {
        selArea = 0;
    }
    else {
        selArea = buf[0];
    }
    if(selArea == 0) {
        pebbleModem = PEBBLE_MODEM_NB_IOT;
        lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_NBIOT);
    } else {
        pebbleModem = PEBBLE_MODEM_LTE_M;
        lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_LTEM);
    }

    if (!mqttCertExist()) {
        ssd1306_clear_screen(0);
        dis_OnelineText(1, ALIGN_CENTRALIZED, "MISS KEY", DIS_NORMAL);
        //while (1);
        return;
    }
}

void modemSettings(void) {
    uint8_t modemMode[3][20] = {
        "NB-IOT         ",
        "LTE-M          ",
        "Exit           ",
    };
    uint8_t buf[100];
    uint8_t *pbuf;
    int cursor = 0, last_cur = 0, selArea = 0, first = 0, i;
    uint8_t index[5];
    
    ssd1306_clear_screen(0);
    buf[0] = 0xFF;
    iotex_local_storage_load(SID_MODEM_WORK_MODE, buf, 1);
    if((buf[0] != 0) && (buf[0] != 1)) {
        selArea = 0;
    }
    else {
        selArea = buf[0];
    }

    modemMode[selArea][15] = 'X';
    for (i = 0; i < sizeof(modemMode)/sizeof(modemMode[0]); i++) {
        dis_OnelineText(i,ALIGN_CENTRALIZED, modemMode[i],DIS_CURSOR(i, cursor)); 
    } 
    modemMode[selArea][15]=' '; 
    ClearKey();

    while (true) {
        last_cur = cursor;
        if (IsUpPressed()) {
            ClearKey();
            cursor--;
            if(cursor < 0)
                cursor = 0;
        } else if(IsDownPressed()) {
            ClearKey();
            cursor++;
            if (cursor > (sizeof(modemMode) / sizeof(modemMode[0]) - 1))
                cursor = (sizeof(modemMode) / sizeof(modemMode[0]) - 1);
        } else if(IsEnterPressed()) {
            ClearKey();
            if (cursor == (sizeof(modemMode) / sizeof(modemMode[0]) - 1))
                break;
            else if (cursor == 0) {
                pebbleModem = PEBBLE_MODEM_NB_IOT;
                lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_NBIOT);
            } else if(cursor == 1) {
                pebbleModem = PEBBLE_MODEM_LTE_M;
                lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_LTEM);
            }
            /*  read modem and writing into  sec */
            if (selArea != cursor) {
                index[0] = cursor;
                index[1] = 0;
                iotex_local_storage_save(SID_MODEM_WORK_MODE, index, 1);
            }
            selArea = cursor;
            last_cur = cursor + 1;
        }
        if (last_cur != cursor) {
            modemMode[selArea][15] = 'X';
            for (i = 0; i < sizeof(modemMode) / sizeof(modemMode[0]); i++) {
                dis_OnelineText(i, ALIGN_CENTRALIZED, modemMode[first + i], DIS_CURSOR(first + i, cursor));
            }
            modemMode[selArea][15] = ' ';
        }
        k_sleep(K_MSEC(100));           
    }      
}
/*
    - M : pebble  will connect to the Main-net
    - T : pebble  will connect to the Test-net
*/
void selectArea(void)
{
    uint8_t showItem[][20] = {
        "MAINIOTX        ",
        "TESTIOTX        ",
        "MAINUSER        ",
        "TESTUSER        "
    };
    uint8_t allArea[5][20] ={
        "MAINIOTX        ",
        "TESTIOTX        "
    };
    uint8_t *pbuf, *pcert;
    int cursor = 0, last_cur = 0, selArea = 0, first = 0, i, areaItems = 0;

    pcert = (uint8_t *) malloc(2400);
    if(pcert == NULL)
        return;
    pbuf = ReadDataFromModem(USER_MAIN_CERT_SEC, pcert, 2048);
    if(pbuf == NULL) {
        pbuf = ReadDataFromModem(USER_TEST_CERT_SEC, pcert, 2048);
        if(pbuf == NULL) {
            memcpy(allArea[2], "Exit            ", 16);
            areaItems = 3;
        } else {
            memcpy(allArea[2], "TESTUSER        ", 16);
            memcpy(allArea[3], "Exit            ", 16);
            areaItems = 4;
        }
    } else {
        pbuf = ReadDataFromModem(USER_TEST_CERT_SEC, pcert, 2048);
        if(pbuf == NULL) {
            memcpy(allArea[2], "MAINUSER        ", 16);
            memcpy(allArea[3], "Exit            ", 16);
            areaItems = 4;
        } else {
            memcpy(allArea[2], "MAINUSER        ", 16);
            memcpy(allArea[3], "TESTUSER        ", 16);
            memcpy(allArea[4], "Exit            ", 16);
            areaItems = 5;
        }
    }
    ssd1306_clear_screen(0);
    pbuf = ReadDataFromModem(MQTT_CERT_INDEX, pcert, 2048);
    if (pbuf != NULL) {
        pbuf[1] = 0;
        selArea = atoi(pbuf);
        if(selArea > areaItems)
            selArea = 0;
    }
    free(pcert);
    allArea[selArea][15] = 'X';
    for (i = 0; i < areaItems && i < 4 ; i++) {
        dis_OnelineText(i,ALIGN_CENTRALIZED, allArea[i],DIS_CURSOR(i, cursor));
    }
    allArea[selArea][15] = ' ';
    ClearKey();
    while (true) {
        last_cur = cursor;
        if (IsUpPressed()) {
            ClearKey();
            cursor--;
            if (cursor < 0)
                cursor = 0; 
            if(cursor < first)
                first = cursor;
        } else if (IsDownPressed()) {
            ClearKey();
            cursor++;
            if (cursor > (areaItems - 1))
                cursor = (areaItems - 1);
            if (cursor > first+3)
                first = cursor - 3;
        } else if (IsEnterPressed()) {
            ClearKey();
            if (cursor == (areaItems - 1))
                break;
            /*  read modem and writing into  sec */
            if (selArea != cursor) {
                for( i =0; i < (sizeof(showItem) / sizeof(showItem[0])); i++ ) {
                    if(!strcmp(showItem[i], allArea[cursor])) {
                        break;
                    }
                }
                updateCert(i);
            }
            selArea = cursor;
            last_cur = cursor + 1;
        }
        if (last_cur != cursor) {
            allArea[selArea][15] = 'X';
            for (i = 0; i < areaItems && i < 4; i++)
            {
                dis_OnelineText(i,ALIGN_CENTRALIZED, allArea[first+i],DIS_CURSOR(first+i, cursor));
            }
            allArea[selArea][15] = ' ';
        }
        k_sleep(K_MSEC(100));
    }
    useDefaultHost();
}

void pebbleInfor(void) {
    uint8_t buf[8][20];
    uint8_t id[20];
    uint8_t sysInfo[100];
    uint8_t buf_sn[100];    
    int cursor = 0, last_cur = 0;
    uint8_t *pbuf;

    memset(sysInfo, 0, sizeof(sysInfo));
    memset(buf, 0, sizeof(buf));
    getSysInfor(sysInfo);
    /*  SN */
    memset(buf, 0, sizeof(buf));
    pbuf = ReadDataFromModem(PEBBLE_DEVICE_SN, buf_sn, sizeof(buf_sn));
    if (pbuf != NULL) {
        /* sprintf(buf[0], "SN:%s", pbuf); */
        strcpy(buf[0], "SN:");
        memcpy(buf[0] + strlen(buf[0]), pbuf, 10); 
        buf[0][13] = 0;
        dis_OnelineText(0, ALIGN_LEFT, buf[0],DIS_NORMAL);
    }    
    /*  IMEI     */
    sprintf(id, "%s", iotex_mqtt_get_client_id());
    strcpy(buf[1], "IMEI:");
    memcpy(buf[1] + strlen(buf[1]), id, 11);
    sprintf(buf[2], "     %s", id+11);
    dis_OnelineText(1, ALIGN_LEFT, buf[1],DIS_NORMAL);
    dis_OnelineText(2, ALIGN_LEFT, buf[2],DIS_NORMAL);
    /*  HW  SDK */
    sprintf(buf[3], "HW:%s SDK:%s", HW_VERSION, SDK_VERSION);
    dis_OnelineText(3, ALIGN_LEFT, buf[3],DIS_NORMAL);   
    /*  app  */
    sprintf(buf[4], "APP:%s", IOTEX_APP_NAME);
    sprintf(buf[5], "VER:%s", RELEASE_VERSION);
    /*  bootloader */
    sprintf(buf[6], "%s", sysInfo);
    /*  modem  */
    sprintf(buf[7], "MD:%s", sysInfo+60);
    /*  open test com port */
    //ComToolInit();
    ClearKey();

    while (true) {
        last_cur = cursor;
        if (IsUpPressed()) {
            ClearKey();
            cursor--;
            if (cursor < 0)
                cursor = 0;
            if (cursor == 2)
                cursor = 1;
        }
        if (IsDownPressed()) {
            ClearKey();
            cursor++;
            if (cursor > 3) {
                cursor = 4;
            } else {
                if (cursor > 1)
                    cursor = 3;
            }
        }
        if (IsEnterPressed()) {
            ClearKey();
            break;
        }

        if (last_cur != cursor) {
            dis_OnelineText(0, ALIGN_LEFT, buf[cursor],DIS_NORMAL);
            dis_OnelineText(1, ALIGN_LEFT, buf[cursor + 1],DIS_NORMAL);
            dis_OnelineText(2, ALIGN_LEFT, buf[cursor + 2],DIS_NORMAL);
            dis_OnelineText(3, ALIGN_LEFT, buf[cursor + 3],DIS_NORMAL);
        }
        k_sleep(K_MSEC(100)); 
    }
    stopTestCom();
}

void sysSet(void) {
    const char mainMenu[3][20] = {
        "Modem Settings",
        "Select Region ",
        "Exit          ",
    };
    int cursor = 0, last_cur = 0, i;

    /*  clear screen */
    ssd1306_clear_screen(0);
    /*  main menu */
    for (i = 0; i < sizeof(mainMenu)/sizeof(mainMenu[0]); i++) {
        dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor));
    }

    ClearKey();
    while (true) {
        last_cur = cursor;
        if (IsUpPressed()) {
            ClearKey();
            cursor--;
            if (cursor < 0)
                cursor = 0;
        } else if (IsDownPressed()) {
            ClearKey();
            cursor++;
            if (cursor > (sizeof(mainMenu) / sizeof(mainMenu[0]) - 1))
                cursor = (sizeof(mainMenu)/sizeof(mainMenu[0]) - 1);
        } else if(IsEnterPressed()) {
            ClearKey();
            if (cursor == 0) {
                modemSettings();
                last_cur = cursor + 1;
            } else if (cursor == 1) {
                selectArea();
                last_cur = cursor + 1;
            } else if (cursor == 2) {
                break;
            } else {
                break;
            }
        }

        if (last_cur != cursor) {
            for (i = 0; i < sizeof(mainMenu) / sizeof(mainMenu[0]); i++) {
                dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor));
            }
            dis_OnelineText(3,ALIGN_LEFT, " ",DIS_NORMAL);
        }
        k_sleep(K_MSEC(100));
    }
}

void MainMenu(void) {
    const char mainMenu[][20] = {
        "System Settings",
        "About Pebble   ",
        "Reboot Pebble  ",
    };
    int cursor = 0, last_cur = 0, i;

    ComToolInit();
    /* Load mqtt data channel configure */
    iotex_mqtt_load_config();
    initBrokeHost();
    if (!checkMenuEntry())
        return;

    /*  clear screen */
    ssd1306_clear_screen(0);
    /*  main menu */
    for (i = 0; i < sizeof(mainMenu)/sizeof(mainMenu[0]); i++) {
        dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor));
    }

    ClearKey();
    while (true) {
        last_cur = cursor;
        if (IsUpPressed()) {
            ClearKey();
            cursor--;
            if (cursor < 0)
                cursor = 0;
        } else if (IsDownPressed()) {
            ClearKey();
            cursor++;
            if (cursor > (sizeof(mainMenu) / sizeof(mainMenu[0]) - 1))
                cursor = (sizeof(mainMenu) / sizeof(mainMenu[0]) - 1);
        } else if (IsEnterPressed()) {
            ClearKey();
            if (cursor == 0) {
                sysSet();
                last_cur = cursor + 1;
            } else if (cursor == 1) {
                pebbleInfor();
                last_cur = cursor+1;
            } else if (cursor == 2) {
                break;
            } else {
                break;
            }
        }

        if (last_cur != cursor) {
            for (i = 0; i < sizeof(mainMenu) / sizeof(mainMenu[0]); i++) {
                dis_OnelineText(i, ALIGN_LEFT, mainMenu[i], DIS_CURSOR(i, cursor));
            }
            dis_OnelineText(3, ALIGN_LEFT, " ", DIS_NORMAL);
        }
        k_sleep(K_MSEC(100));
    }
    ssd1306_clear_screen(0); 
    pebbleBackGround(0); 
    ssd1306_display_on();  
}

void appEntryDetect(void) {
    pebbleBackGround(0);
    /*  system  startup check proposer status  */
    devRegSet(DEV_REG_START);
    /*  Need to upgrade? */
    if (isUpKeyStartupPressed()) {
        /*  system will be upgraded via OTA */
        hintString(htConnecting, HINT_TIME_FOREVER);
        devRegSet(DEV_UPGRADE_ENTRY);
    }
}

void pebbleBackGround(uint32_t sel) {
    uint8_t disBuf[20];
    uint8_t showArea[][9] = {
        "MAINIOTX",
        "TESTIOTX",
        "MAINUSER",
        "TESTUSER"
    };

    sys_mutex_lock(&iotex_hint_mutex, K_FOREVER);
    dis_OnelineText(2, ALIGN_LEFT, "",DIS_NORMAL);
    switch(sel) {
        case 0:
            strcpy(disBuf, IOTEX_APP_NAME);
            dis_OnelineText(1, ALIGN_CENTRALIZED, disBuf,DIS_NORMAL);
            strcpy(disBuf, "v"RELEASE_VERSION);
            dis_OnelineText(2, ALIGN_CENTRALIZED, disBuf,DIS_NORMAL);
            if(pebbleModem == PEBBLE_MODEM_NB_IOT) {
                strcpy(disBuf, "NB-IOT");
            } else {
                strcpy(disBuf, "LTE-M");
            }
            strcpy(disBuf+strlen(disBuf), "  ");
            strcpy(disBuf+strlen(disBuf), showArea[pebbleContractNet]);
            dis_OnelineText(3, ALIGN_CENTRALIZED, disBuf,DIS_NORMAL);
            break;
        default:
            ssd1306_display_logo();
            break;
    }
    sys_mutex_unlock(&iotex_hint_mutex);
}

bool pebbleWorksAtNBIOT(void) {
    return (pebbleModem == PEBBLE_MODEM_NB_IOT);
}
bool pebbleWorsAtLTEM(void) {
    return (pebbleModem == PEBBLE_MODEM_LTE_M);
}

void anotherWorkMode(void) {
    if(pebbleWorksAtNBIOT()) {
        pebbleModem = PEBBLE_MODEM_LTE_M;
        lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_LTEM);
    } else {
        pebbleModem = PEBBLE_MODEM_NB_IOT;
        lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_NBIOT);
    }
    pebbleBackGround(0);
    setDefaultWorkMode();
}

void setDefaultWorkMode(void) {
    uint8_t index[5];
    if(pebbleWorksAtNBIOT()) {
        index[0] = 0;
    } else {
        index[0] = 1;
    }
    index[1] = 0;
    iotex_local_storage_save(SID_MODEM_WORK_MODE, index, 1);
}
