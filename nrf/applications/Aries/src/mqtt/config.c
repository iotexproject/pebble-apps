#include "mqtt.h"
#include "config.h"
#include "cJSON.h"
#include "cJSON_os.h"
#include "nvs/local_storage.h"
#include "ui.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "package.pb.h"
#include "ver.h"

LOG_MODULE_REGISTER(config, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define  DEFAULT_CHANNEL  8183
#define  DEFAULT_UPLOAD_PERIOD   60

/* Global configure */
static iotex_mqtt_config __config = {
    .data_channel = DEFAULT_CHANNEL,
    .bulk_upload = 0,
    .bulk_upload_sampling_cnt = 0,
    .bulk_upload_sampling_freq = 0,
    .upload_period = DEFAULT_UPLOAD_PERIOD, 

    .appName = "Riverrock",
    .appVersion = "v1.0.0+",
    .configName = "Common",
    .configVersion = "v1.0.0",
    .trusttreamTopic = "",
    .preciseGPS = false
};
struct sys_mutex iotex_config_mutex;
static uint8_t firmwareUrl[200] = "https://pebble-ota.s3.ap-east-1.amazonaws.com/app_update.bin";
extern const uint8_t firmwareVersion[];

uint16_t getDevChannel(void) {
    return __config.data_channel;
}

int packDevConf(uint8_t *buffer, uint32_t size) {
    SensorConfig confData = SensorConfig_init_zero;
    BinPackage binpack = BinPackage_init_zero;
    uint32_t uint_timestamp;
    char esdaSign[65];
    int sinLen;

    confData.has_bulkUpload = true;
    confData.bulkUpload = __config.bulk_upload;
    confData.has_dataChannel = true;
    confData.dataChannel = __config.data_channel;
    confData.has_uploadPeriod = true;
    confData.uploadPeriod = __config.upload_period;
    confData.has_bulkUploadSamplingCnt = true;
    confData.bulkUploadSamplingCnt = __config.bulk_upload_sampling_cnt;
    confData.has_bulkUploadSamplingFreq = true;
    confData.bulkUploadSamplingFreq = __config.bulk_upload_sampling_freq;
    confData.has_beep = true;
    confData.beep = 1000;
    confData.has_firmware = true;
    strcpy(confData.firmware, firmwareVersion);
    confData.has_deviceConfigurable = true;
    confData.deviceConfigurable = false;
    pb_ostream_t enc_datastream;
    enc_datastream = pb_ostream_from_buffer(binpack.data.bytes, sizeof(binpack.data.bytes));
    if (!pb_encode(&enc_datastream, SensorConfig_fields, &confData)) {
        /* encode error happened */
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_datastream));
        return 0;
    }
    uint_timestamp = atoi(iotex_modem_get_clock(NULL));
    binpack.data.size = enc_datastream.bytes_written;
    binpack.data.bytes[enc_datastream.bytes_written] = (char)((uint_timestamp & 0xFF000000) >> 24);
    binpack.data.bytes[enc_datastream.bytes_written + 1] = (char)((uint_timestamp & 0x00FF0000) >> 16);
    binpack.data.bytes[enc_datastream.bytes_written + 2] = (char)((uint_timestamp & 0x0000FF00) >> 8);
    binpack.data.bytes[enc_datastream.bytes_written + 3] = (char)(uint_timestamp & 0x000000FF);
    buffer[0] = 0;
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = (uint8_t)BinPackage_PackageType_CONFIG;
    memcpy(buffer + 4, binpack.data.bytes, enc_datastream.bytes_written + 4);
    LOG_INF("enc_datastream.bytes_written+8 :%d \n", enc_datastream.bytes_written + 8);
    doESDASign(buffer, enc_datastream.bytes_written + 8, esdaSign, &sinLen);
    memcpy(binpack.signature, esdaSign, 64);
    binpack.timestamp = uint_timestamp;
    binpack.type = BinPackage_PackageType_CONFIG;

    pb_ostream_t enc_packstream;
    enc_packstream = pb_ostream_from_buffer(buffer, size);
    if (!pb_encode(&enc_packstream, BinPackage_fields, &binpack)) {
        /* encode error happened */
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_packstream));
        return 0;
    }

    return enc_packstream.bytes_written;   
}

static bool save_mqtt_config(void) {
    /* Delete sid, only save one piece config, do not save history */
    if (iotex_local_storage_del(SID_MQTT_DATA_CHANNEL_CONFIG)) {
        return false;
    }
    return iotex_local_storage_save(SID_MQTT_DATA_CHANNEL_CONFIG, &__config, sizeof(__config)) == 0;
}

bool iotex_mqtt_is_bulk_upload(void) {
    bool bulkOrnot;
    config_mutex_lock();
    bulkOrnot = (__config.bulk_upload != 0);
    config_mutex_unlock();
    return bulkOrnot;
}

uint16_t iotex_mqtt_get_data_channel(void) {
    uint16_t channel;
    config_mutex_lock();
    channel = __config.data_channel;
    config_mutex_unlock();
    return channel;
}

bool iotex_precise_gps(void) {
    bool ret;
    config_mutex_lock();
    ret = __config.preciseGPS;
    config_mutex_unlock();
    return ret; 
}
uint8_t *iotex_get_truStreamTopic(void) {
    uint8_t *topic = NULL;
    config_mutex_lock();
    topic = __config.trusttreamTopic;
    config_mutex_unlock();
    return topic;
}

uint16_t iotex_mqtt_get_upload_period(void) {
    uint16_t ret;
    config_mutex_lock();
    ret = __config.bulk_upload ? 0 : (__config.upload_period < 10 ? 10 : __config.upload_period);
    config_mutex_unlock();
    return ret;
}

uint16_t iotex_mqtt_get_bulk_upload_count(void) {
    uint16_t ret;
    config_mutex_lock();
    ret = __config.bulk_upload_sampling_cnt;
    config_mutex_unlock();
    return ret;
}

uint16_t iotex_mqtt_get_sampling_frequency(void) {
    uint16_t ret;
    config_mutex_lock();
    ret = __config.bulk_upload_sampling_freq;
    config_mutex_unlock();
    return ret;
}

uint16_t iotex_mqtt_get_current_upload_count(void) {
    uint16_t ret;
    config_mutex_lock();
    ret = __config.current_upload_cnt;
    config_mutex_unlock();
    return ret;
}

uint16_t iotex_mqtt_get_current_sampling_count(void) {
    uint16_t ret;
    config_mutex_lock();
    ret = __config.current_sampling_cnt;
    config_mutex_unlock();
    return ret;
}

bool iotex_mqtt_is_need_sampling(void) {
    bool ret;
    config_mutex_lock();
	LOG_INF("current_sampling_cnt:%d, bulk_upload_sampling_cnt:%d\n", __config.current_sampling_cnt, __config.bulk_upload_sampling_cnt);
    ret =  __config.current_sampling_cnt < __config.bulk_upload_sampling_cnt;
    config_mutex_unlock();
    return ret;
}

bool iotex_mqtt_is_bulk_upload_over(void) {
    bool ret;
    config_mutex_lock();
	ret = __config.current_upload_cnt >= __config.bulk_upload_sampling_cnt;
    config_mutex_unlock();
    return ret;
}

bool iotex_mqtt_inc_current_upload_count(void) {
    config_mutex_lock();
    /* Increase upload count */
    __config.current_upload_cnt++;
    /* Save upload count, for breakpoint retransmission */
    if (__config.current_upload_cnt >= __config.bulk_upload_sampling_cnt) {
        /* All of data is uploaded, switch sampling mode */
        __config.current_upload_cnt = 0;
        __config.current_sampling_cnt = 0;
        save_mqtt_config();
        config_mutex_unlock();
        return true;
    }
    save_mqtt_config();
    config_mutex_unlock();
    return false;
}

bool iotex_mqtt_inc_current_sampling_count(void) {
    /* Increase sampling count */
    __config.current_sampling_cnt++;
    /* Save sampling count, for breakpoint resampling */
    if (__config.current_sampling_cnt >= __config.bulk_upload_sampling_cnt) {
        __config.current_upload_cnt = 0;
        save_mqtt_config();
        config_mutex_unlock();
        return true;
    }
    save_mqtt_config();
    config_mutex_unlock();
    return false;
}

void config_mutex_lock(void) {
    sys_mutex_lock(&iotex_config_mutex, K_FOREVER);
}

void config_mutex_unlock(void) {
    sys_mutex_unlock(&iotex_config_mutex);
}

static void print_mqtt_config(const iotex_mqtt_config *config, const char *title) {
    LOG_INF("%s: bulk_upload: [%s], data_channel:[0x%04x], upload_period[%u], "
        "bulk_upload_sampling_cnt[%u], bulk_upload_sampling_freq[%u],"
        "current_upload_cnt[%u], current_sampling_cnt[%u]\n",
        title, config->bulk_upload ? "yes" : "no", config->data_channel, config->upload_period,
        config->bulk_upload_sampling_cnt, config->bulk_upload_sampling_freq,
        config->current_upload_cnt, config->current_sampling_cnt);
}

static uint8_t *findDigit(uint8_t *buf) {
    uint8_t *pcheck = buf, pend = strlen(buf) + buf;

    while(pcheck != pend) {
        if((*pcheck) >= '0' && (*pcheck) <='9')
            return pcheck;
        pcheck++;
    }
    return NULL;
}

static void parsTopic(uint8_t *out_topic, uint8_t *in_str) {
    uint8_t buf[20];
    uint8_t *pstart = NULL, *pend = NULL;
    out_topic[0] = 0;
    pstart = strchr(in_str, '$');
    if(pstart != NULL) {
        memcpy(out_topic, in_str, pstart-in_str);
        out_topic[pstart-in_str] = 0;
        pstart++;
        if(pstart != (in_str + strlen(in_str))) {
            pend = strchr(pstart, '/');
            if(pend) {
                memcpy(buf, pstart, pend-pstart);
                buf[pend-pstart] = 0;
                if(!strcmp(buf, "imei")) {
                    snprintf(out_topic+strlen(out_topic), "%s",iotex_mqtt_get_client_id());
                }
                else {
                    LOG_ERR("Downloaded config - trusttreamTopic:%s not surport\n", buf);
                    out_topic[0] = 0;
                    return;
                }
                strcpy(out_topic+strlen(out_topic), pend);
            }
        }
    } else {
        strcpy(out_topic, in_str);
    }
}

/*
    parse jscon update configure
    return 0
*/
static int iotex_mqtt_parse_config(const uint8_t *payload, uint32_t len, iotex_mqtt_config *config) {
    char config_buffer[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];
    uint32_t dat;
    int ret = 1;
    uint8_t *ptmp;

    if (len >= CONFIG_MQTT_PAYLOAD_BUFFER_SIZE) {
        LOG_ERR("config size not enough len: %d, conf_size:%d\n",len, CONFIG_MQTT_PAYLOAD_BUFFER_SIZE);
        return 0;
    }
    memcpy(config_buffer, payload, len);
    config_buffer[len] = 0;
#ifdef CONFIG_DEBUG_MQTT_CONFIG
    LOG_INF("[%s]: Received mqtt json config: [%u]%s\n", __func__, len, config_buffer);
#endif

    cJSON *appName = NULL;
    cJSON *data_channel = NULL;
    cJSON *upload_period = NULL;
    cJSON *appVersion = NULL;
    cJSON *configName = NULL;
    cJSON *configVersion = NULL;
    cJSON *trusttreamTopic = NULL;
    cJSON *preciseGPS = NULL;
    cJSON *root_obj = cJSON_Parse(config_buffer);
    if (!root_obj) {
        const char *err_ptr = cJSON_GetErrorPtr();
        if (err_ptr) {
            LOG_ERR("[%s:%d] error before: %s\n", __func__, __LINE__, err_ptr);
        }
        ret = 0;
        goto cleanup;
    }
    appName = cJSON_GetObjectItem(root_obj, "appName");
    data_channel = cJSON_GetObjectItem(root_obj, "data_channel");
    upload_period = cJSON_GetObjectItem(root_obj, "upload_period");
    appVersion = cJSON_GetObjectItem(root_obj, "appVersion");
    configName = cJSON_GetObjectItem(root_obj, "configName");
    configVersion = cJSON_GetObjectItem(root_obj, "configVersion");
    trusttreamTopic = cJSON_GetObjectItem(root_obj, "trusttreamTopic");
    preciseGPS = cJSON_GetObjectItem(root_obj, "preciseGPS");
    
    /* Notice: 0 ==> false, otherwise true */
    if (strlen(appName->valuestring) && (appName && cJSON_IsString(appName))) {
        if(strlen(appName->valuestring) < sizeof(config->appName)) {
            if(!strcmp(appName->valuestring, IOTEX_APP_NAME)) {
                strcpy(config->appName, appName->valuestring);
            }
            else {
                LOG_ERR("recerived app name : %s, but this app is : %s\n", appName->valuestring, IOTEX_APP_NAME);
                ret = 0;
                goto cleanup;
            }
        }
        else {
            LOG_ERR("appName is too long!\n");
            ret = 0;
            goto cleanup;
        }
    }

    if (appVersion && cJSON_IsString(appVersion)) {
        if(strlen(appVersion->valuestring) && (strlen(appVersion->valuestring) < sizeof(config->appVersion))) {
            if(*(appVersion->valuestring+strlen(appVersion->valuestring)-1) == '+') {
                ptmp = findDigit(appVersion->valuestring);
                if(ptmp == NULL || (strncmp(RELEASE_VERSION, ptmp,strlen(RELEASE_VERSION)) < 0)) {
                    LOG_ERR("Version not supported\n");
                    ret = 0;
                    goto cleanup;
                }
            }
            else {
                if(strcmp(appVersion->valuestring, RELEASE_VERSION)) {
                    LOG_ERR("Received version:%s not supported, app firm version : %s\n", appVersion->valuestring,RELEASE_VERSION);
                    ret = 0;
                    goto cleanup;
                }
            }
            strcpy(config->appVersion, appVersion->valuestring);
        }
    }
    if (configName && cJSON_IsString(configName)) {
        if(strlen(configName->valuestring) && (strlen(configName->valuestring) < sizeof(config->configName))) {
            strcpy(config->configName, configName->valuestring);
        }
    }
    if (configVersion && cJSON_IsString(configVersion)) {
        if(strlen(configVersion->valuestring) && (strlen(configVersion->valuestring) < sizeof(config->configVersion))) {
            strcpy(config->configVersion, configVersion->valuestring);
        }
    }

    if (upload_period && cJSON_IsString(upload_period)) {
        config->upload_period = atoi(upload_period->valuestring);
        if(config->upload_period > 24*60*60)
            config->upload_period = SENSOR_UPLOAD_PERIOD;
        if(config->upload_period < 10)
            config->upload_period = 10;
    }

    if (data_channel && cJSON_IsNumber(data_channel)) {
        config->data_channel = data_channel->valueint;
        if(config->data_channel > DEFAULT_CHANNEL)
            config->data_channel = DEFAULT_CHANNEL;
    }    

    if (trusttreamTopic && cJSON_IsString(trusttreamTopic)) {
        if(strlen(trusttreamTopic->valuestring) && (strlen(trusttreamTopic->valuestring) < sizeof(config->trusttreamTopic))) {
            parsTopic(config->trusttreamTopic, trusttreamTopic->valuestring);
        }
    }
    if (preciseGPS && cJSON_IsBool(preciseGPS)) {
        config->preciseGPS = cJSON_IsTrue(preciseGPS) ? true : false;
    }    

#ifdef CONFIG_DEBUG_MQTT_CONFIG
    print_mqtt_config(config, __func__);
#endif

cleanup:
    cJSON_Delete(root_obj);
    return ret;
}

/* Parse configure and save it to nvs */
void iotex_mqtt_update_config(const uint8_t *payload, uint32_t len) {
    int ret;    

    config_mutex_lock();
    if (!(ret = iotex_mqtt_parse_config(payload, len, &__config))) {
        config_mutex_unlock();
        LOG_ERR("[%s:%d]: Parse configure failed!\n", __func__, __LINE__);
        return;
    }
    /* If enable bulk upload delete local sampling data */
    if (ret == 2) {
        __config.current_sampling_cnt = 0;
        __config.current_upload_cnt = 0;
        iotex_local_storage_del(SID_MQTT_BULK_UPLOAD_DATA);
        LOG_INF("Local sampling data deleted!!!\n");
    }
    /* Save new configure */
    if (!save_mqtt_config()) {
        config_mutex_unlock();
        LOG_ERR("[%s:%d]: Save configure failed!\n", __func__, __LINE__);
        return;
    }
    config_mutex_unlock();
}

/* Load confifure from nvs and apply */
void iotex_mqtt_load_config(void) {
    if (!iotex_local_storage_load(SID_MQTT_DATA_CHANNEL_CONFIG, &__config, sizeof(__config))) {
        print_mqtt_config(&__config, __func__);
    }

    sys_mutex_init(&iotex_config_mutex);
}

void set_block_size(uint32_t size)
{
    __config.size_of_block = size;
}

uint32_t get_block_size(void)
{
    return __config.size_of_block;
}

uint16_t get_his_block(void)
{
    if (__config.current_upload_cnt < __config.bulk_upload_sampling_cnt)
        return (__config.bulk_upload_sampling_cnt - __config.current_upload_cnt -1);
    else
        return 0;
}

int iotex_mqtt_update_url(const uint8_t *payload, uint32_t len) {
    cJSON *Uri = NULL;
    cJSON *root_obj = cJSON_Parse(payload);
    if (!root_obj) {
        const char *err_ptr = cJSON_GetErrorPtr();
        if (err_ptr) {
            LOG_ERR("[%s:%d] error before: %s\n", __func__, __LINE__, err_ptr);
        }
        return -1;
    }
    Uri = cJSON_GetObjectItem(root_obj, "uri");
    if (Uri && cJSON_IsString(Uri)) {
        /* strcpy(url, firmwareUri->valuestring); */
        strcpy(firmwareUrl,  Uri->valuestring);
        LOG_INF("firmwareUrl:%s \n", firmwareUrl);
    }
    cJSON_Delete(root_obj);
    return 0;
}

int compareVersion(char * version1, char * version2){
    for(int i=0,j=0;i<strlen(version1)||j<strlen(version2);){
        int v1=0,v2=0;
        while(i<strlen(version1)&&version1[i]!='.'){
            v1=v1*10+(version1[i]-'0');
            i++;
        }
        i++;
        while(j<strlen(version2)&&version2[j]!='.'){
            v2=v2*10+(version2[j]-'0');
            j++;
        }
        j++;
        if(v1>v2){
            return 1;
        }
        if(v1<v2){
            return -1;
        }
    }
    return 0;
}

void clrfirmwareUrl(void) {
    memset(firmwareUrl, 0, sizeof(firmwareUrl));
}

bool isUrlNull(void) {
    return (strlen(firmwareUrl) == 0);
}

uint8_t *getOTAUrl(void) {
    return firmwareUrl;
}