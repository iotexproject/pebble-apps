#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <power/reboot.h>
#include <sys/mutex.h>

#include "hints_data.h"
#include "display.h"

#include "ver.h"
#include "keyBoard.h"

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

extern uint8_t s_chDispalyBuffer[128][8];
void hintInit(void)
{
    hintAliveTime= 0;
    htLanguage = HT_LANGUAGE_EN;
    sys_mutex_init(&iotex_hint_mutex);  
}

uint8_t hintTimeDec(void)
{
    if(hintAliveTime){
        hintAliveTime--;
        if(!hintAliveTime)
            return 0;
    }
    else
    {
        return 1;
    }
    
    return  hintAliveTime;
}

void  hintString(uint8_t *str[], uint8_t tim)
{
    int  len,lines,i,j,k;
    uint8_t xpos,ypos;
    uint8_t *dis;
    sys_mutex_lock(&iotex_hint_mutex, K_FOREVER);
    hintAliveTime = tim;    
    dis = str[htLanguage]; 
    clearDisBuf(0,5);    
    if(htLanguage == HT_LANGUAGE_EN){        
        len = strlen(dis);        
        if(len > 16)
        {
            lines = ((len<<3)+HINT_WIDTH-1)/HINT_WIDTH;       
            if(lines <= HINT_MAX_LINE){
                xpos = 0;
                ypos = HINT_FONT;
            }
            else
            {
                printk("Hints too long:%s, lines:%d\n", dis, lines);
                sys_mutex_unlock(&iotex_hint_mutex);
                return;
            }          
        }
        else
        {
            //ypos = (HINT_HEIGHT+2)/2;
            ypos = HINT_HEIGHT/2;            
            xpos = (HINT_WIDTH - (len << 3))/2;
        }  
        ssd1306_display_string(xpos, ypos, dis, 16, 1);      
    }
    else
    {
        len = str[HT_LANG_CN_SIZE];       
        if(len > 256)
        {
            lines = len/256;       
            if(lines <= HINT_MAX_LINE){
                xpos = 0;
                ypos = HINT_FONT;
            }
            else
            {
                printk("Chinese Hints too long\n");
                sys_mutex_unlock(&iotex_hint_mutex);
                return;
            }          
        }
        else
        {
            //ypos = (HINT_HEIGHT+2)/2;
            ypos = HINT_HEIGHT/2;            
            xpos = (HINT_WIDTH - (len >>1))/2;            
        }        
        ypos = 7 - ypos/8;  
printk("xpos:%d, ypos:%d, len:%d\n", xpos,ypos,len);         
        for(j=0; j < len; ypos -=2 )
        { 
            k = (len - j) >= 256 ? 128 : ((len -j)/2 +xpos);                  
            for(i =xpos; i < k; i++,j++)
            {
                s_chDispalyBuffer[i][ypos] = dis[j];
                s_chDispalyBuffer[i][ypos-1] = dis[j+(k-xpos)];
            }  
            j += (k-xpos); 
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
const uint8_t textLine[]={0,16,32,48};
void dis_OnelineText(uint32_t line, uint32_t flg,  uint8_t *str)
{
    uint8_t xpos,ypos;
    uint32_t len = strlen(str); 
    //printk("str:%s, len:%d\n", str,len);
    clearDisBuf(6-2*line,7-2*line);       
    if(len > 16)
    {
        printk("Hints too long:%s\n", str);        
        return;         
    }
    else
    {                  
        // left align
        if(flg)
        {
            xpos = 0;
        }
        else  // centered
        {
            xpos = (HINT_WIDTH - (len << 3))/2;
        }
        ypos =textLine[line];
    }  
    ssd1306_display_string(xpos, ypos, str, 16, 1); 
    //ssd1306_refresh_lines(line*2,line*2+1);
    ssd1306_refresh_lines(6-2*line,7-2*line);
}



// startup menu

bool checkMenuEntry(void)
{
    if(isUpKeyStartupPressed())
    {
        return true;
    }
    return false;
}

void MainMenu(void)
{
    uint8_t buf[20];
    uint8_t sysInfo[100];
    if(!checkMenuEntry())
        return;
    memset(sysInfo, 0, sizeof(sysInfo)); 
    getSysInfor(sysInfo);    
    // SN
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "SN:%s", sysInfo);
    dis_OnelineText(0, 1, buf);
    // HW  SDK
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "HW:%s SDK:%s", HW_VERSION, SDK_VERSION);
    dis_OnelineText(1, 1, buf);    
    // app 
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "APP:%s", APP_VERSION+9);
    dis_OnelineText(2, 1, buf); 
    // modem 
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "MD:%s", sysInfo+60);
    dis_OnelineText(3, 1, buf);  

    ClearKey();
    while(true)
    {
        k_sleep(K_MSEC(500));
        if(IsEnterPressed())
            break;            
    }
    ssd1306_clear_screen(0); 
    ssd1306_display_logo(); 
    ssd1306_display_on();  
}

