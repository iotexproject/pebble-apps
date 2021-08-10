#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <net/cloud.h>
#include <net/socket.h>
#include <net/nrf_cloud.h>
#include "devReg.h"
#include "ui.h"
#include "cJSON.h"
#include "cJSON_os.h"
#include "ecdsa.h"
#include "mqtt/mqtt.h"
#include "display.h"
#include "watchdog.h"


#include "pb_decode.h"
#include "pb_encode.h"
#include "package.pb.h"

static uint32_t devRegStatus = DEV_REG_STOP;

static uint8_t walletAddr[200];


//static  unsigned char json_buffer[300];
extern void startOTA(uint8_t *url);

bool IsDevReg(void)
{
    return (devRegStatus>=DEV_REG_START);
}
void devRegSet(enum DEV_REG_STA esta)
{
    devRegStatus = esta;
} 
uint32_t devRegGet(void)
{
    return devRegStatus;
}
int iotex_mqtt_get_wallet(const uint8_t *payload, uint32_t len) 
{
    cJSON *walletAddress = NULL;
    cJSON *regStatus = NULL;
    cJSON *root_obj = cJSON_Parse(payload);

    if (!root_obj) {
        const char *err_ptr = cJSON_GetErrorPtr();

        if (!err_ptr) {
            printk("[%s:%d] error before: %s\n", __func__, __LINE__, err_ptr);
        }
        return 0;
    }
    regStatus = cJSON_GetObjectItem(root_obj, "status");
    if(!regStatus)
    {
        printk("Poll proposal error\n");
        cJSON_Delete(root_obj);
        return 0;
    }
    if(regStatus->valueint == 1){
        walletAddress = cJSON_GetObjectItem(root_obj, "proposer");
        if(walletAddress != NULL)
        {
            printk("proposer: %s \n", walletAddress->valuestring);
            strcpy(walletAddr, walletAddress->valuestring);
            cJSON_Delete(root_obj);
            return 1;        
        }
    }
    else if(regStatus->valueint == 2){
        return 2;
    }

    cJSON_Delete(root_obj);
    return 0; 
}

extern int hexStr2Bin(char *str, char *bin);
int  SignAndSend(void)
{
    char esdaSign[65];
    char jsStr[130];  
    char *json_buf = NULL; 
    int  sinLen; 
    uint32_t  uint_timestamp;

    ConfirmPackage confirmAdd = ConfirmPackage_init_zero;

    uint_timestamp = atoi(iotex_modem_get_clock(NULL));

    json_buf = malloc(DATA_BUFFER_SIZE);
    if(json_buf == NULL)
        return  -1;
    printk("walletAddr:%s\n", walletAddr);
    confirmAdd.owner.size = hexStr2Bin(walletAddr+2,confirmAdd.owner.bytes);

    confirmAdd.owner.bytes[confirmAdd.owner.size] = (char)((uint_timestamp & 0xFF000000) >> 24);
    confirmAdd.owner.bytes[confirmAdd.owner.size+1] = (char)((uint_timestamp & 0x00FF0000) >> 16);
    confirmAdd.owner.bytes[confirmAdd.owner.size+2] = (char)((uint_timestamp & 0x0000FF00) >> 8);
    confirmAdd.owner.bytes[confirmAdd.owner.size+3] = (char)(uint_timestamp & 0x000000FF);       

    doESDA_sep256r_Sign(confirmAdd.owner.bytes,confirmAdd.owner.size+4,esdaSign,&sinLen);   

    memcpy(confirmAdd.signature, esdaSign, 64);   

    confirmAdd.timestamp = uint_timestamp;
    confirmAdd.channel = getDevChannel();

    pb_ostream_t enc_packstream;
	enc_packstream = pb_ostream_from_buffer(json_buf, DATA_BUFFER_SIZE);	
	if (!pb_encode(&enc_packstream, ConfirmPackage_fields, &confirmAdd))
	{
		//encode error 
		printk("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_packstream));
        free(json_buf);
		return -1;
	}    
    
    publish_dev_ownership(json_buf, enc_packstream.bytes_written);   
    free(json_buf); 
}

void waitForOtaOver(void)
{
    while(devRegGet() != DEV_UPGRADE_COMPLETE)
    {
        if(devRegGet() == DEV_UPGRADE_STARED)
        {
            startOTA(getOTAUrl());
            devRegSet(DEV_UPGRADE_DOWNLOADING);            
        }
        k_sleep(K_MSEC(5000));
    }
}

void stopTaskBeforeOTA(void)
{
    stopWatchdog();
    stopHeartBeat();
    stopMqtt();
}

void mainStatus(struct mqtt_client *client)
{    
    switch(devRegGet())
    {
        case DEV_REG_STATUS:
            publish_dev_query("", 0);
            break;
        case DEV_REG_PRESS_ENTER:
            if(IsEnterPressed())
            {
                ClearKey();
                devRegSet(DEV_REG_POLL_FOR_WALLET);
                hintString(htRegRequest,HINT_TIME_FOREVER); 
            }
            break;
        case DEV_REG_POLL_FOR_WALLET:
            publish_dev_query("", 0);
            break;
        case DEV_REG_ADDR_CHECK:
            sprintf(htRegaddrChk_en,"%s", walletAddr);
            hintString(htRegaddrChk,HINT_TIME_FOREVER);
            if(IsEnterPressed())
            {
                devRegSet(DEV_REG_SIGN_SEND);
                hintString(htRegWaitACK,HINT_TIME_FOREVER);
            }
            break;
        case DEV_REG_SIGN_SEND:
            SignAndSend();
            devRegSet(DEV_REG_POLL_STATE);  
            //  only for test
            //k_sleep(K_MSEC(1000));
            //devRegSet(DEV_REG_SUCCESS); 
            break;
        case DEV_REG_POLL_STATE:
            publish_dev_query("", 0);
            
            //devRegSet(DEV_REG_SUCCESS); 
            break;
        case DEV_REG_SUCCESS:
            hintString(htRegSuccess, HINT_TIME_DEFAULT);
            StartSendEnvData(30);
            devRegSet(DEV_REG_STOP);                    
            break;
        case DEV_UPGRADE_STARED:
            stopTaskBeforeOTA();
            startOTA(getOTAUrl());
            devRegSet(DEV_REG_STOP); 
            hintString(htUpgrading, HINT_TIME_DEFAULT);
            printk("will restart ???\n");
            waitForOtaOver();
            //iotex_mqtt_upgrade_over(client,0);
            break;
        case DEV_REG_STOP:
            
            break;
        default :
            break;
    }
}

// 5s everyloop
void  iotexDevBinding(struct pollfd *fds, struct mqtt_client *client)
{   
    int err, tmCount=0;
    //hintString("iotexDevBinding", HINT_TIME_DEFAULT);
    
    while(1)
    {       
        err = poll(fds, 1, CONFIG_MAIN_BASE_TIME);
        if (err < 0) {
            printk("ERROR: poll %d\n", errno);            
            break;
        }        
        if ((fds->revents & POLLIN) == POLLIN) {
            err = mqtt_input(client);

            if (err != 0) {
                printk("ERROR: mqtt_input %d\n", err);                
                break;
            }
        }
        if ((fds->revents & POLLERR) == POLLERR) {
            printk("POLLERR\n");            
            break;
        }

        if ((fds->revents & POLLNVAL) == POLLNVAL) {
            printk("POLLNVAL\n");            
            break;
        }

        if ((fds->revents & POLLNVAL) == POLLNVAL) {
            printk("POLLNVAL\n");
            
            break;
        } 
        err = mqtt_live(client);
        if ((err != 0) && (err != -EAGAIN)) {
            printk("ERROR: mqtt_live %d\n", err);        
            break;
        } 
        //printk("kjkljljljljlj\n");
        //if(!err)
        //{
        //    iotex_mqtt_heart_beat(client, 0);
        //}     
        printk("mqtt live ????\n");

        mainStatus(client);       
        sta_Refresh();
        
        if(!hintTimeDec())
        {
            ssd1306_display_logo();
        }
        // check  registration  status every 1s
        //k_sleep(K_MSEC(1000));      
    }
    mqtt_disconnect(client);

}
