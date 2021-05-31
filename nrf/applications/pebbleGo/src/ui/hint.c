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
                printk("Hints too long\n");
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