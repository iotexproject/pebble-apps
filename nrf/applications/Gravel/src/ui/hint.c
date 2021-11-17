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

#define HINT_WIDTH    128
#define HINT_HEIGHT  56
#define HINT_XPOS_START  0
#define HINT_YPOS_START  16
#define HINT_FONT   16
#define HINT_MAX_LINE   (HINT_HEIGHT/HINT_FONT)

#define HINT_CENTRAL_LINE   24


static uint8_t hintAliveTime;
uint8_t htLanguage;
struct sys_mutex iotex_hint_mutex;
static uint32_t pubCounts = 0;
const uint8_t textLine[] = { 0, 16, 32, 48 };

extern uint8_t s_chDispalyBuffer[128][8];
extern  const  uint8_t *pmqttBrokerHost;
extern  void ledFlahsWorkerInit(void);

const uint8_t AreaSec[5][2]={
    {ASIA_PACIFIC_CERT_SEC, ASIA_PACIFIC_KEY_SEC},
    {EUROPE_FRANKFURT_CERT_SEC, EUROPE_FRANKFURT_KEY_SEC},
    {MIDDLE_EAST_CERT_SEC, MIDDLE_EAST_KEY_SEC},
    {NORTH_AMERICA_CERT_SEC, NORTH_AMERICA_KEY_SEC},
    {SOUTH_AMERICA_CERT_SEC, SOUTH_AMERICA_KEY_SEC}
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
        if(!hintAliveTime)
            return 0;
    }
    else
    {
        return 1;
    }
    return hintAliveTime;
}

void clrHint(void) {
    if(hintAliveTime) {
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
    strcpy(disBuf, "App:"APP_NAME);    
    dis_OnelineText(1, ALIGN_LEFT, disBuf,DIS_NORMAL);
    strcpy(disBuf, "Ver:"RELEASE_VERSION);    
    dis_OnelineText(2, ALIGN_LEFT, disBuf,DIS_NORMAL);    
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


/*  startup menu */

bool checkMenuEntry(void)
{
    return isDownKeyStartupPressed();
}

void updateCert(int selArea)
{
    uint8_t *pcert,*pkey,*proot;
    uint8_t *pbuf_cert, *pbuf_key,*pbuf_root;
    uint8_t index[5];
    pcert = malloc(2048);
    pkey = malloc(2048);
    proot = malloc(2048);
    if((pcert == NULL) || (pkey == NULL) || (proot == NULL))
    {
        LOG_ERR("mem not enough !\n");
        if(pcert)
            free(pcert);
        if(pkey)
            free(pkey);
        if(proot)
            free(proot);                  
        return;
    }

    pbuf_cert = ReadDataFromModem(AreaSec[selArea][0], pcert, 2048);
    pbuf_key = ReadDataFromModem(AreaSec[selArea][1], pkey, 2048);
    pbuf_root = ReadDataFromModem(AWS_ROOT_CA, proot, 2048);
    if((pbuf_cert == NULL) || (pbuf_key == NULL) || (pbuf_root == NULL))
    {
        LOG_ERR("read cert error \n");
        goto out;
    }
    pbuf_cert[strlen(pbuf_cert)-3] = 0;
    pbuf_key[strlen(pbuf_key)-3] = 0;
    pbuf_root[strlen(pbuf_root)-3] = 0;     
    WriteCertIntoModem(pbuf_cert, pbuf_key, pbuf_root);
    pmqttBrokerHost = mqttBrokerHost[selArea];
    itoa(selArea, index, 10);
    index[1] = 0;
    WritDataIntoModem(MQTT_CERT_INDEX, index);
out:
    free(pcert);
    free(pkey);
    free(proot);
}

void initBrokeHost(void)
{
    uint8_t buf[100];
    uint8_t *pbuf;
    int selArea;

    pbuf = ReadDataFromModem(MQTT_CERT_INDEX, buf, sizeof(buf));
    if(pbuf != NULL)
    {
        pbuf[1] = 0;
        selArea = atoi(pbuf);
        pmqttBrokerHost = mqttBrokerHost[selArea];
    } 
    pbuf = ReadDataFromModem(MODEM_MODE_SEL, buf, sizeof(buf));
    if(pbuf != NULL)
    {
        pbuf[1] = 0;
        selArea = atoi(pbuf);
        if(selArea == 0) {
            lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_NBIOT);
        }
        else {
            lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_LTEM);
        }        
    }

    if(!mqttCertExist())
    {
        ssd1306_clear_screen(0);
        dis_OnelineText(1, ALIGN_CENTRALIZED, "MISS KEY", DIS_NORMAL);
        while(1);
        return;
    }        
}

void modemSettings(void) {
    uint8_t   modemMode[3][20] = {
        "NB-IOT         ",
        "LTE-M          ",    
        "Exit           ", 
    };
    uint8_t buf[100];
    uint8_t *pbuf;
    int  cursor = 0, last_cur = 0, selArea = 0, first = 0,i; 
    uint8_t index[5];  
    
    ssd1306_clear_screen(0);
    pbuf = ReadDataFromModem(MODEM_MODE_SEL, buf, sizeof(buf));
    if(pbuf != NULL)
    {
        pbuf[1] = 0;
        selArea = atoi(pbuf);
    }
    modemMode[selArea][15]='X';
    for(i = 0; i < sizeof(modemMode)/sizeof(modemMode[0]); i++)
    {
        dis_OnelineText(i,ALIGN_CENTRALIZED, modemMode[i],DIS_CURSOR(i, cursor)); 
    } 
    modemMode[selArea][15]=' '; 
    ClearKey();
    while(true)
    {    
        last_cur = cursor;     
        if(IsUpPressed())
        {
            ClearKey();
            cursor--;
            if(cursor < 0)
                cursor = 0;
        }
        else if(IsDownPressed())
        {
            ClearKey();
            cursor++;
            if(cursor > (sizeof(modemMode)/sizeof(modemMode[0])-1))
                cursor = (sizeof(modemMode)/sizeof(modemMode[0])-1);
        }        
        else if(IsEnterPressed())
        {
            ClearKey();
            if(cursor == (sizeof(modemMode)/sizeof(modemMode[0])-1))
                break;
            else if(cursor == 0)
            {
                lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_NBIOT);
            }
            else if(cursor == 1)
            {
                lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_LTEM);
            }
            /*  read modem and writing into  sec */
            if(selArea != cursor) {
                itoa(cursor, index, 10);
                index[1] = 0;
                WritDataIntoModem(MODEM_MODE_SEL, index);
            }               
            selArea = cursor;
            last_cur = cursor+1;
        }        
        if(last_cur != cursor)
        {
            modemMode[selArea][15]='X';
            for(i = 0; i < sizeof(modemMode)/sizeof(modemMode[0]); i++)
            {
                dis_OnelineText(i,ALIGN_CENTRALIZED, modemMode[first+i],DIS_CURSOR(first+i, cursor)); 
            } 
            modemMode[selArea][15]=' ';
        }                
        k_sleep(K_MSEC(100));           
    }      
}
void selectArea(void)
{
    uint8_t   allArea[][20] = {
        "Asia            ",
/*        
        "Europe          ",
        "Middle East     ",
        "North America   ",
        "South America   ",
*/        
        "Exit            "
    };
    uint8_t buf[100];
    uint8_t *pbuf;
    int  cursor = 0, last_cur = 0, selArea = 0, first = 0,i;
    
    ssd1306_clear_screen(0);
    pbuf = ReadDataFromModem(MQTT_CERT_INDEX, buf, sizeof(buf));
    if(pbuf != NULL)
    {
        pbuf[1] = 0;
        selArea = atoi(pbuf);
    }   
    allArea[selArea][15]='X';
    for(i = 0; i < (sizeof(allArea)/sizeof(allArea[0])); i++)
    {
        dis_OnelineText(i,ALIGN_CENTRALIZED, allArea[i],DIS_CURSOR(i, cursor)); 
    } 
    allArea[selArea][15]=' ';
    ClearKey();
    while(true)
    {    
        last_cur = cursor;     
        if(IsUpPressed())
        {
            ClearKey();
            cursor--;
            if(cursor < 0)
                cursor = 0;
            if(cursor < 3)
                first = cursor;
        }
        else if(IsDownPressed())
        {
            ClearKey();
            cursor++;
            if(cursor > (sizeof(allArea)/sizeof(allArea[0])-1))
                cursor = (sizeof(allArea)/sizeof(allArea[0])-1);
            if(cursor > 3)
                first = cursor-3;
        }        
        else if(IsEnterPressed())
        {
            ClearKey();
            if(cursor == (sizeof(allArea)/sizeof(allArea[0])-1))
                break;
            /*  read modem and writing into  sec */
            if(selArea != cursor)
                updateCert(cursor);
            selArea = cursor;
            last_cur = cursor+1;
        }        
        if(last_cur != cursor)
        {
            allArea[selArea][15]='X';
            for(i = 0; i < (sizeof(allArea)/sizeof(allArea[0])); i++)
            {
                dis_OnelineText(i,ALIGN_CENTRALIZED, allArea[first+i],DIS_CURSOR(first+i, cursor)); 
            } 
            allArea[selArea][15]=' ';
        }                
        k_sleep(K_MSEC(100));           
    }
}

void pebbleInfor(void) {
    uint8_t buf[8][20];
    uint8_t id[20];
    uint8_t sysInfo[100];
    uint8_t buf_sn[100];    
    int  cursor = 0, last_cur = 0;
    uint8_t *pbuf;

    memset(sysInfo, 0, sizeof(sysInfo)); 
    memset(buf, 0, sizeof(buf));
    getSysInfor(sysInfo);    
    /*  SN */
    memset(buf, 0, sizeof(buf));
    pbuf = ReadDataFromModem(PEBBLE_DEVICE_SN, buf_sn, sizeof(buf_sn));
    if(pbuf != NULL)
    {
        /* sprintf(buf[0], "SN:%s", pbuf); */
        strcpy(buf[0], "SN:");
        memcpy(buf[0]+strlen(buf[0]), pbuf, 10); 
        buf[0][13] = 0;
        dis_OnelineText(0, ALIGN_LEFT, buf[0],DIS_NORMAL);
    }    
    /*  IMEI     */
    sprintf(id, "%s", iotex_mqtt_get_client_id());
    strcpy(buf[1], "IMEI:");
    memcpy(buf[1]+strlen(buf[1]), id, 11);
    sprintf(buf[2], "     %s", id+11);
    dis_OnelineText(1, ALIGN_LEFT, buf[1],DIS_NORMAL);
    dis_OnelineText(2, ALIGN_LEFT, buf[2],DIS_NORMAL);
    /*  HW  SDK */
    sprintf(buf[3], "HW:%s SDK:%s", HW_VERSION, SDK_VERSION);
    dis_OnelineText(3, ALIGN_LEFT, buf[3],DIS_NORMAL);   
    /*  app  */
    sprintf(buf[4], "APP:%s", APP_NAME);
    sprintf(buf[5], "VER:%s", RELEASE_VERSION);
    /*  bootloader */
    sprintf(buf[6], "%s", sysInfo);
    /*  modem  */
    sprintf(buf[7], "MD:%s", sysInfo+60);
    /*  open test com port */
    ComToolInit();
    ClearKey();
    while(true)
    {
        last_cur = cursor;
        if(IsUpPressed())
        {
            ClearKey();
            cursor--;
            if(cursor < 0)
                cursor = 0;
            if(cursor == 2)
                cursor = 1;
        }
        if(IsDownPressed())
        {
            ClearKey();
            cursor++;
            if(cursor > 3) {
                cursor = 4;
            }
            else {
                if(cursor > 1)
                    cursor = 3;   
            }       
        }        
        if(IsEnterPressed()){
            ClearKey();
            break; 
        }

        if(last_cur != cursor)
        {
            dis_OnelineText(0, ALIGN_LEFT, buf[cursor],DIS_NORMAL);
            dis_OnelineText(1, ALIGN_LEFT, buf[cursor+1],DIS_NORMAL);
            dis_OnelineText(2, ALIGN_LEFT, buf[cursor+2],DIS_NORMAL);
            dis_OnelineText(3, ALIGN_LEFT, buf[cursor+3],DIS_NORMAL);
        }
        k_sleep(K_MSEC(100)); 
        outputSn();          
    }
    stopTestCom();
}
void sysSet(void) {
    const char mainMenu[3][20]={
        "Modem Settings",
        "Select Region ",
        "Exit          ",               
    };
    int  cursor = 0, last_cur = 0, i;

    /*  clear screen */
    ssd1306_clear_screen(0);
    /*  main menu */
    for(i = 0; i < sizeof(mainMenu)/sizeof(mainMenu[0]); i++)
    {
        dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor)); 
    } 
    ClearKey();
    while(true)
    {
        last_cur = cursor;
        if(IsUpPressed())
        {
            ClearKey();
            cursor--;
            if(cursor < 0)
                cursor = 0;
        }
        else if(IsDownPressed())
        {
            ClearKey();
            cursor++;
            if(cursor > (sizeof(mainMenu)/sizeof(mainMenu[0]) - 1))
                cursor = (sizeof(mainMenu)/sizeof(mainMenu[0]) - 1);         
        }        
        else if(IsEnterPressed())
        {
            ClearKey();
            if(cursor == 0)
            {
                modemSettings();
                last_cur = cursor+1;
            }
            else if(cursor == 1)
            {
                selectArea();
                last_cur = cursor+1;
            }
            else if(cursor == 2)
            {
                break;
            }
            else
            {
                break;
            }
        }
        if(last_cur != cursor)
        {
            for(i = 0; i < sizeof(mainMenu)/sizeof(mainMenu[0]); i++)
            {
                dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor)); 
            } 
            dis_OnelineText(3,ALIGN_LEFT, " ",DIS_NORMAL);
        }         
        k_sleep(K_MSEC(100));           
    }
    
}
void MainMenu(void) {
    const char mainMenu[][20]={
        "System Settings",
        "About Pebble   ",
        "Reboot Pebble  ",        
    };
    int  cursor = 0, last_cur = 0, i;

    initBrokeHost();
    if(!checkMenuEntry())
        return;   
    /*  clear screen */
    ssd1306_clear_screen(0);
    /*  main menu */
    for(i = 0; i < sizeof(mainMenu)/sizeof(mainMenu[0]); i++)
    {
        dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor)); 
    } 
    ClearKey();
    while(true)
    {
        last_cur = cursor;
        if(IsUpPressed())
        {
            ClearKey();
            cursor--;
            if(cursor < 0)
                cursor = 0;
        }
        else if(IsDownPressed())
        {
            ClearKey();
            cursor++;
            if(cursor > (sizeof(mainMenu)/sizeof(mainMenu[0]) - 1))
                cursor = (sizeof(mainMenu)/sizeof(mainMenu[0]) - 1);                     
        }        
        else if(IsEnterPressed())
        {
            ClearKey();
            if(cursor == 0)
            {
                sysSet();
                last_cur = cursor+1;
            }
            else if(cursor == 1)
            {
                pebbleInfor();
                last_cur = cursor+1;
            }
            else if(cursor == 2)
            {
                break;
            }
            else
            {
                break;
            }
        }
        if(last_cur != cursor)
        {
            for(i = 0; i < sizeof(mainMenu)/sizeof(mainMenu[0]); i++)
            {
                dis_OnelineText(i,ALIGN_LEFT, mainMenu[i],DIS_CURSOR(i, cursor)); 
            } 
            dis_OnelineText(3,ALIGN_LEFT, " ",DIS_NORMAL);
        }         
        k_sleep(K_MSEC(100));           
    }
    ssd1306_clear_screen(0); 
    ssd1306_display_logo(); 
    ssd1306_display_on();  
}

void appEntryDetect(void) {
    /*  system  startup check proposer status  */
    devRegSet(DEV_REG_START);
    /*  Need to upgrade? */
    if(isUpKeyStartupPressed()) {
        /*  system will be upgraded via OTA */
        hintString(htConnecting, HINT_TIME_FOREVER); 
        devRegSet(DEV_UPGRADE_ENTRY);
    }
}
