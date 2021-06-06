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
    int ret = -1;
    cJSON *walletAddress = NULL;
    cJSON *message_obj = NULL;    
    cJSON *root_obj = cJSON_Parse(payload);
    if (!root_obj) {
        const char *err_ptr = cJSON_GetErrorPtr();

        if (!err_ptr) {
            printk("[%s:%d] error before: %s\n", __func__, __LINE__, err_ptr);
        }   
        return ret;     
    }
    message_obj = cJSON_GetObjectItem(root_obj, "message");
    walletAddress = cJSON_GetObjectItem(message_obj, "walletAddress");
    if (walletAddress && cJSON_IsString(walletAddress)) {
        strcpy(walletAddr, walletAddress->valuestring);
printk("walletAddr:%s \n", walletAddr);        
    }
    cJSON_Delete(root_obj);    
    return ret;      
}


int  SignAndSend(void)
{
    int ret = -1;
    char esdaSign[65];
    char jsStr[130];  
    char *json_buf; 
    int  sinLen; 

    cJSON *root_obj = cJSON_CreateObject();
    cJSON *msg_obj = cJSON_CreateObject();
    
    if (!root_obj||!msg_obj) {        
        goto out;
    }
    cJSON_AddItemToObject(root_obj, "message", msg_obj);    

    // wallet address
    cJSON *walletAddr_obj = cJSON_CreateString(walletAddr);
    if(!walletAddr_obj  || json_add_obj(msg_obj, "walletAddress", walletAddr_obj))
        goto out;        
    // imei
    cJSON *imei_obj = cJSON_CreateString(iotex_mqtt_get_client_id());
    if(!imei_obj  || json_add_obj(msg_obj, "imei", imei_obj))
        goto out; 
        
    // public key
    //get_ecc_public_key(jsStr);
    json_buf = readECCPubKey();
    json_buf[128] = 0;
    cJSON *pubkey_obj = cJSON_CreateString(json_buf);
    if(!pubkey_obj  || json_add_obj(msg_obj, "publicKey", pubkey_obj))
        goto out;                  

    json_buf = cJSON_PrintUnformatted(root_obj);   
printk("root_obj :%s \n", json_buf);    
    doESDA_sep256r_Sign(json_buf,strlen(json_buf),esdaSign,&sinLen);   
    hex2str(esdaSign, sinLen,jsStr);
    cJSON_free(json_buf);
    memcpy(esdaSign,jsStr,64);
    esdaSign[64] = 0; 
    cJSON *sign_obj = cJSON_CreateObject();
    if(!sign_obj)
        goto out;  
    cJSON_AddItemToObject(root_obj, "signature", sign_obj); 
    
    cJSON *esdaSign_r_Obj = cJSON_CreateString(esdaSign);
    if(!esdaSign_r_Obj  || json_add_obj(sign_obj, "r", esdaSign_r_Obj))
        goto out;
    
    cJSON *esdaSign_s_Obj = cJSON_CreateString(jsStr+64);
    if(!esdaSign_s_Obj  || json_add_obj(sign_obj, "s", esdaSign_s_Obj))
        goto out; 
            
    //memset(output->buf, 0, strlen(output->buf));   
    json_buf = cJSON_PrintUnformatted(root_obj);    
    //output->len = strlen(json_buf);
printk("json_buf :%s \n", json_buf);   

    publish_dev_ownership(json_buf);
    ret = 0;
out:   
    cJSON_free(json_buf);
    cJSON_Delete(root_obj); 
    return ret;      
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
        case DEV_REG_SIGN_SEND:
            hintString(htRegWaitACK,HINT_TIME_FOREVER);            
            SignAndSend();
            devRegSet(DEV_REG_WAIT_ACK);  
            //  only for test
            k_sleep(K_MSEC(1000));
            devRegSet(DEV_REG_SUCCESS); 
            break;
        case DEV_REG_SUCCESS:
            hintString(htRegSuccess, HINT_TIME_DEFAULT);
            devRegSet(DEV_REG_START);        
            break;
        case DEV_UPGRADE_STARED:
            stopTaskBeforeOTA();
            startOTA(getOTAUrl());
            devRegSet(DEV_REG_STOP); 
            hintString(htUpgrading, HINT_TIME_DEFAULT);
            printk("will restart ???\n");
            waitForOtaOver();
            iotex_mqtt_upgrade_over(client,0);
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
        if(!err)
        {
            iotex_mqtt_heart_beat(client, 0);
        }     
        printk("mqtt live ????\n");

        mainStatus(client);       
        tmCount++;
        if(tmCount == (STA_REFRESH_T/CONFIG_MAIN_BASE_TIME))
        {
            tmCount = 0;
            sta_Refresh();
        }
        if(!hintTimeDec())
        {
            ssd1306_display_logo();
        }
        // check  registration  status every 1s
        //k_sleep(K_MSEC(1000));      
    }
    mqtt_disconnect(client);

}
