

#include "antenna_sdk/src/u128.h"
#include "antenna_sdk/src/iotex_emb.h"
#include "antenna_sdk/src/abi_read_contract.h"
#include "mqtt.h"


// test contract
//const char *pebble = "io1zclqa7w3gxpk47t3y3g9gzujgtl44lastfth28"; // to be replaced by actual contract address
//  mainnet contract
const char *pebble = "io1a8qeke954ncyddc0ek3vlq5xpz54f0l7lyx8wg";
const char *method = "c5934222";
//static char data[33] = "0000000000000000000000000000000000333532363536313030373934363132"; // hex-encoded string of 32-byte device ID
iotex_st_contract_data contract_data = {};


extern struct mqtt_endpoint *pEndPoint;

void getDeviceID(char *data)
{
#if 1
    char *pImei;
    int i;   
    memcpy(data, "0000000000000000000000000000000000", 34);
    pImei = iotex_modem_get_imei();
    for(i=0;i<30;i+=2)
    {
        *(data+34+i) = '3';
        *(data+34+i+1) = *pImei++;
    }
    *(data+34+i)=0;
#else
    memcpy(data,"0000000000000000000000000000000000333532363536313030373934363132", 64);
    data[64]=0;
#endif
    //printk("device id: %s\n", data);
}
bool isSubExpired(void)
{
    return(pEndPoint->sub_status == SUBSCRIPTION_EXPIRED);
}

void iotex_free_endpoint(void)
{
    if(pEndPoint) {
        free(pEndPoint);
        pEndPoint = NULL;
    }
}
int iotex_peble_startup_check(void)
{
    char *pStr = NULL;
    int ret = -1;
    uint64_t  start;
    uint32_t  duration;
    const char *endpoint,*token;
    iotex_st_chain_meta  chain = {};
    // read the device's order info from contract
    if (iotex_emb_read_contract_by_addr(pebble, method, pEndPoint->dev_id, &contract_data) != 0) {
        // error handling
        printk("iotex_emb_read_contract_by_addr error \n");
        return  -1;
    }

    // parse order info
    start = abi_get_order_start(contract_data.data, contract_data.size); // starting block subscriber paid to receive data
    duration = abi_get_order_duration(contract_data.data, contract_data.size); // number of blocks subscriber paid to receive data
    endpoint = abi_get_order_endpoint(contract_data.data, contract_data.size); // subscriber's storage endpoint address
    token = abi_get_order_token(contract_data.data, contract_data.size); // subscriber's storage endpoint token

    // calculate the duration we need to send IoT data
    
    if (iotex_emb_get_chain_meta(&chain) != 0) {
        // error handling
        printk("iotex_emb_get_chain_meta error \n");
        return -2;
    }

    printk("start:%llu, duration:%u, chain.height:%llu\n", start,duration,chain.height);
    printk("endpoint:%s\n", endpoint ); 
    if ((start + (uint64_t)duration) > chain.height) {        
        duration = (uint32_t)(start + (uint64_t)duration - chain.height) * 5;
    } else {
        // subscription already expired
        duration = 0;
    }

    ret = SUBSCRIPTION_EXPIRED;
    printk("duration:%u\n", duration);
    //if ((duration <= 0) && endpoint) {
    if ((duration > 0)&& endpoint) {
        // start sending data to the subscriber's storage endpoint for 'duration' seconds
        pStr = strstr(endpoint, ":");        
        if((pEndPoint->sub_status == SUBSCRIPTION_EXPIRED) && pStr)
        {
            memcpy(pEndPoint->endpoint, endpoint, pStr-endpoint);
            pEndPoint->endpoint[pStr-endpoint] = 0;
            pEndPoint->port = atoi(pStr+1);
            pEndPoint->sub_status = SUBSCRIPTION_ACTIVE;
            ret = SUBSCRIPTION_ACTIVE;
        }
    }  
    // free the endpoint and token
    free(endpoint);
    endpoint = NULL;
    free(token);
    token = NULL;
    return  ret;

}

int iotex_pebble_endpoint_poll(void)
{
    int  ret = 0;
    if((!isSubExpired()) && (!iotex_mqtt_is_connected()))
        return 1;
    // read the device's order info from contract
    if (iotex_emb_read_contract_by_addr(pebble, method, pEndPoint->dev_id, &contract_data) != 0) {
        // error handling
        printk("iotex_emb_read_contract_by_addr error \n");
        return  -1;
    }
    // parse order info
    uint64_t start = abi_get_order_start(contract_data.data, contract_data.size); // starting block subscriber paid to receive data
    uint32_t duration = abi_get_order_duration(contract_data.data, contract_data.size); // number of blocks subscriber paid to receive data
    const char *endpoint = abi_get_order_endpoint(contract_data.data, contract_data.size); // subscriber's storage endpoint address
    const char *token = abi_get_order_token(contract_data.data, contract_data.size); // subscriber's storage endpoint token

    // calculate the duration we need to send IoT data
    iotex_st_chain_meta chain = {};
    if (iotex_emb_get_chain_meta(&chain) != 0) {
        // error handling
        printk("iotex_emb_get_chain_meta error \n");
        return -2;
    } 
    printk("start:%llu, duration:%u, chain.height:%llu\n", start,duration,chain.height); 
    printk("endpoint:%s\n", endpoint ); 
    
    if (start + (uint64_t)duration > chain.height) {
        duration = (uint32_t)(start + (uint64_t)duration - chain.height) * 5;
    } else {
        // subscription already expired
        duration = 0;
    }

    if (duration > 0) {
        // start sending data to the subscriber's storage endpoint for 'duration' seconds    
        if(pEndPoint->sub_status == SUBSCRIPTION_EXPIRED)
        {            
            sys_reboot(0);            
        }  
        ret = 2;
    }
    else
    {
        if(pEndPoint->sub_status == SUBSCRIPTION_ACTIVE)
        {            
            sys_reboot(0);
            //ret = 2;
        }
    }
    printk("duration:%u\n", duration);
    // free the endpoint and token
    free(endpoint);
    endpoint = NULL;
    free(token);
    token = NULL;
    return  ret;
}
int iotex_init_endpoint(char *endpoint, int port)
{
    int retry=3;
    pEndPoint = malloc(sizeof(struct mqtt_endpoint));
    if(pEndPoint == NULL)
    {
        printk("iotex_init_endpoint: not enouhg heap space\n");
        return -1;
    }
    getDeviceID(pEndPoint->dev_id);
    memcpy(pEndPoint->endpoint, endpoint, strlen(endpoint));
    pEndPoint->port = port;
    pEndPoint->sub_status = SUBSCRIPTION_EXPIRED;
    while((iotex_peble_startup_check()<0) && retry)
    {
        retry--;
        printk("iotex_peble_startup_check retry \n");
        k_sleep(K_MSEC(1000));        
    }
    return (pEndPoint->sub_status == SUBSCRIPTION_EXPIRED) ? 0 : 1;
}




