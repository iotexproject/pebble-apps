
#include <zephyr.h>
#include <sys/util.h>
#include <drivers/gps.h>
#include <modem/lte_lc.h>
#include <drivers/gpio.h>
#include <drivers/uart.h>
#include <stdlib.h>

#include "modem_helper.h"
#include "nvs/local_storage.h"


typedef struct 
{
    uint8_t boot_ver[20];
    uint8_t hw_ver[20];
    uint8_t sdk_ver[20];
    uint8_t modem_ver[20];
    uint8_t app_ver[20];
}PebbleSYSParam;



static int_fast32_t formatSysParam(PebbleSYSParam* psys)
{
    uint8_t buf[60];
    uint32_t i;
    uint8_t sysInfo[200];
    uint8_t *pbuf;
    pbuf = ReadDataFromModem(PEBBLE_SYS_PARAM_SEC,sysInfo,sizeof(sysInfo));
    if(pbuf == NULL){
        return 0;
    }
    memcpy(psys, pbuf, sizeof(PebbleSYSParam));
    memset(psys->modem_ver,0, sizeof(psys->modem_ver));
    getModeVer(buf);
    strcpy(psys->modem_ver, buf);
    //memset(psys->modem_ver+strlen(psys->modem_ver), 'F', sizeof(psys->modem_ver)- strlen(psys->modem_ver));
    for(i = 0; i < sizeof(psys->app_ver); i++)
    {      
        if(psys->app_ver[i] == 'F')
        {
            memset(psys->app_ver+i, 0, sizeof(psys->app_ver) - i);
            break;           
        }
    }
    for(i = 0; i < sizeof(psys->sdk_ver); i++)
    {    
        if(psys->sdk_ver[i] == 'F')
        {
            memset(psys->sdk_ver+i, 0, sizeof(psys->sdk_ver) - i); 
            break;
        } 
    }
    for(i = 0; i < sizeof(psys->boot_ver); i++)
    {  
        if(psys->boot_ver[i] == 'F')
        {
            memset(psys->boot_ver+i, 0, sizeof(psys->boot_ver) - i);
            break;
        } 
    }
    for(i = 0; i < sizeof(psys->hw_ver); i++)
    {              
        if(psys->hw_ver[i] == 'F')
        {
            memset(psys->hw_ver+i, 0, sizeof(psys->hw_ver) - i);
            break;
        }                       
    }   
}

void getSysInfor(uint8_t *buf)
{
    PebbleSYSParam  sysParam;

    formatSysParam(&sysParam);

    memcpy(buf, &sysParam, sizeof(PebbleSYSParam));
    
}   
