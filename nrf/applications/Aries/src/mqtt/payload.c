#include <stdio.h>
#include <net/cloud.h>
#include <logging/log.h>
#include "cJSON.h"
#include "cJSON_os.h"
#include "mqtt.h"
#include "config.h"
#include "hal/hal_adc.h"
#include "nvs/local_storage.h"
#include "modem/modem_helper.h"
#include "bme/bme680_helper.h"
#include "icm/icm42605_helper.h"
#include "gps_controller.h"
#include "light_sensor/tsl2572.h"

#include "pb_decode.h"
#include "pb_encode.h"
#include "package.pb.h"

LOG_MODULE_REGISTER(payload, CONFIG_ASSET_TRACKER_LOG_LEVEL);

double latitude;
double longitude;



int json_add_obj(cJSON *parent, const char *str, cJSON *item) {
    cJSON_AddItemToObject(parent, str, item);
    return 0;
}

static int json_add_str(cJSON *parent, const char *str, const char *item) {
    cJSON *json_str;
    json_str = cJSON_CreateString(item);
    if (!json_str)
        return -ENOMEM;

    return json_add_obj(parent, str, json_str);
}

int iotex_mqtt_get_devinfo_payload(struct mqtt_payload *output) {
    int ret = 0;
    cJSON *root_obj = cJSON_CreateObject();
    cJSON *vbat_obj = cJSON_CreateNumber(iotex_modem_get_battery_voltage());
    cJSON *snr_obj = cJSON_CreateNumber(iotex_model_get_signal_quality());
    if (!root_obj || !vbat_obj || !snr_obj) {
        ret = -ENOMEM;
        goto cleanup;
    }

    /* Format device info */
    ret += json_add_str(root_obj, "Device", iotex_mqtt_get_client_id());
    ret += json_add_str(root_obj, "timestamp", iotex_modem_get_clock(NULL));
    ret += json_add_obj(root_obj, "VBAT", vbat_obj);
    ret += json_add_obj(root_obj, "SNR", snr_obj);

    if (ret != 0) {
        goto cleanup;
    }

    output->buf = cJSON_PrintUnformatted(root_obj);
    output->len = strlen(output->buf);

cleanup:
    if (root_obj)
        cJSON_Delete(root_obj);
    if (vbat_obj)
        cJSON_Delete(vbat_obj);
    if (snr_obj)
        cJSON_Delete(snr_obj);
    return ret;
}

int iotex_mqtt_get_env_sensor_payload(struct mqtt_payload *output) {
    iotex_storage_bme680 data;

    if (iotex_bme680_get_sensor_data(&data)) {
        return -1;
    }

    int ret = 0;
    const char *device = "BME680";
    cJSON *root_obj = cJSON_CreateObject();
    cJSON *humidity = cJSON_CreateNumber(data.humidity);
    cJSON *pressure = cJSON_CreateNumber(data.pressure);
    cJSON *temperature = cJSON_CreateNumber(data.temperature);
    cJSON *gas_resistance = cJSON_CreateNumber(data.gas_resistance);

    if (!root_obj || !humidity || !pressure || !temperature || !gas_resistance) {
        ret = -ENOMEM;
        goto cleanup;
    }

    ret += json_add_str(root_obj, "Device", device);
    ret += json_add_obj(root_obj, "humidity", humidity);
    ret += json_add_obj(root_obj, "pressure", pressure);
    ret += json_add_obj(root_obj, "temperature", temperature);
    ret += json_add_obj(root_obj, "gas_resistance", gas_resistance);
    ret += json_add_str(root_obj, "timestamp", iotex_modem_get_clock(NULL));

    if (ret != 0) {
        goto cleanup;
    }

    output->buf = cJSON_PrintUnformatted(root_obj);
    output->len = strlen(output->buf);

cleanup:
    if (root_obj)
        cJSON_Delete(root_obj);
    if (humidity)
        cJSON_Delete(humidity);
    if (pressure)
        cJSON_Delete(pressure);
    if (temperature)
        cJSON_Delete(temperature);
    if (gas_resistance)
        cJSON_Delete(gas_resistance);
    return ret;
}

int iotex_mqtt_get_action_sensor_payload(struct mqtt_payload *output) {
    int i;
    int ret = 0;
    const char *device = "ICM42605";
    iotex_storage_icm42605 data;
    int gyroscope[ARRAY_SIZE(data.gyroscope)];
    int accelerometer[ARRAY_SIZE(data.accelerometer)];

    if (iotex_icm42605_get_sensor_data(&data)) {
        return -1;
    }

    for (i = 0; i < ARRAY_SIZE(data.gyroscope); i++) {
        gyroscope[i] = data.gyroscope[i];
    }

    for (i = 0; i < ARRAY_SIZE(data.accelerometer); i++) {
        accelerometer[i] = data.accelerometer[i];
    }

    cJSON *root_obj = cJSON_CreateObject();
    cJSON *temperature_obj = cJSON_CreateNumber(data.temperature);
    cJSON *gyroscope_obj = cJSON_CreateIntArray(gyroscope, ARRAY_SIZE(gyroscope));
    cJSON *accelerometer_obj = cJSON_CreateIntArray(accelerometer, ARRAY_SIZE(accelerometer));

    if (!root_obj || !temperature_obj || !gyroscope_obj || !accelerometer_obj) {
        ret = -ENOMEM;
        goto cleanup;
    }

    ret += json_add_str(root_obj, "Device", device);
    ret += json_add_obj(root_obj, "gyroscope", gyroscope_obj);
    ret += json_add_obj(root_obj, "temperature", temperature_obj);
    ret += json_add_obj(root_obj, "accelerometer", accelerometer_obj);
    ret += json_add_str(root_obj, "timestamp", iotex_modem_get_clock(NULL));

    if (ret != 0) {
        goto cleanup;
    }

    output->buf = cJSON_PrintUnformatted(root_obj);
    output->len = strlen(output->buf);

cleanup:
    if (root_obj)
        cJSON_Delete(root_obj);
    if (temperature_obj)
        cJSON_Delete(temperature_obj);
    if (gyroscope_obj)
        cJSON_Delete(gyroscope_obj);
    if (accelerometer_obj)
        cJSON_Delete(accelerometer_obj);
    return ret;
}

bool iotex_mqtt_sampling_data_and_store(uint16_t channel) {
    uint8_t buffer[128];
    uint32_t write_cnt = 0;
    iotex_storage_bme680 env_sensor;
    iotex_storage_icm42605 action_sensor;
    double  timestamp;
    float AmbientLight;
    uint16_t vol_Integer;

    /* Check current sampling count */
    if (!iotex_mqtt_is_need_sampling()) {
        return true;
    }

    /* Sampling data */
    if (DATA_CHANNEL_ENV_SENSOR & channel) {
        if (iotex_bme680_get_sensor_data(&env_sensor)) {
            return false;
        }
    }

    if (DATA_CHANNEL_ACTION_SENSOR & channel) {
        if (iotex_icm42605_get_sensor_data(&action_sensor)) {
            return false;
        }
    }

    /* Pack data to a buffer then store to nvs, unpack with the same sequence when uploaded */

    /* Snr */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_SNR)) {
        buffer[write_cnt++] = iotex_model_get_signal_quality();
    }

    /* Vbatx100 */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_VBAT)) {
        vol_Integer = (uint16_t)(iotex_modem_get_battery_voltage() * 1000);
        buffer[write_cnt++] = (uint8_t)(vol_Integer & 0x00FF);
        buffer[write_cnt++] = (uint8_t)((vol_Integer>>8) & 0x00FF);
    }


    /* Env sensor gas */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GAS)) {
        memcpy(buffer + write_cnt, &env_sensor.gas_resistance, sizeof(env_sensor.gas_resistance));
        write_cnt += sizeof(env_sensor.gas_resistance);
    }

    /* Env sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP)) {
        memcpy(buffer + write_cnt, &env_sensor.temperature, sizeof(env_sensor.temperature));
        write_cnt += sizeof(env_sensor.temperature);
    }

    /* Env sensor pressure */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_PRESSURE)) {
        memcpy(buffer + write_cnt, &env_sensor.pressure, sizeof(env_sensor.pressure));
        write_cnt += sizeof(env_sensor.pressure);
    }

    /* Env sensor humidity */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_HUMIDITY)) {
        memcpy(buffer + write_cnt, &env_sensor.humidity, sizeof(env_sensor.humidity));
        write_cnt += sizeof(env_sensor.humidity);
    }

    /* Env sensor light */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_LIGHT_SENSOR)) {
        AmbientLight = iotex_Tsl2572ReadAmbientLight();
        memcpy(buffer + write_cnt, &AmbientLight, sizeof(AmbientLight));
        write_cnt += sizeof(AmbientLight);
    }

    /* Action sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP2)) {
        memcpy(buffer + write_cnt, &action_sensor.temperature, sizeof(action_sensor.temperature));
        write_cnt += sizeof(action_sensor.temperature);
    }

    /* Action sensor gyroscope data */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GYROSCOPE)) {
        memcpy(buffer + write_cnt, action_sensor.gyroscope, sizeof(action_sensor.gyroscope));
        write_cnt += sizeof(action_sensor.gyroscope);
    }

    /* Action sensor accelerometer */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_ACCELEROMETER)) {
        memcpy(buffer + write_cnt, action_sensor.accelerometer, sizeof(action_sensor.accelerometer));
        write_cnt += sizeof(action_sensor.accelerometer);
    }

    /*  save timestamp */
    timestamp = iotex_modem_get_clock_raw(NULL);
    memcpy(buffer + write_cnt, &timestamp, sizeof(timestamp));
    write_cnt += sizeof(timestamp);
    
    /*  save bytes of onece write */
    set_block_size(write_cnt);    

    /* Save sampling data to nvs */
    return iotex_local_storage_save(SID_MQTT_BULK_UPLOAD_DATA, buffer, write_cnt) == 0;
}

int iotex_mqtt_get_selected_payload(uint16_t channel, struct mqtt_payload *output) {
    int i;
    int ret = -ENOMEM;
    iotex_storage_bme680 env_sensor;
    iotex_storage_icm42605 action_sensor;
    char esdaSign[65];
    char jsStr[130];
    int sinLen;
    float AmbientLight = 0.0;
    char random[17];
    cJSON *root_obj = cJSON_CreateObject();
    cJSON *msg_obj = cJSON_CreateObject();
    cJSON *snr = NULL;
    cJSON *vbat = NULL;
    cJSON *lat = NULL;
    cJSON *lon = NULL;
    cJSON *gas_resistance = NULL;
    cJSON *temperature = NULL;
    cJSON *pressure = NULL;
    cJSON *humidity = NULL;
    cJSON *light = NULL;
    cJSON *temperature2 = NULL;
    cJSON *gyroscope_obj = NULL;
    cJSON *accelerometer_obj = NULL;
    cJSON *random_obj = NULL;
    cJSON *ecc_pub_obj = NULL;
    cJSON *sign_obj = NULL;
    cJSON *esdaSign_r_Obj = NULL;
    cJSON *esdaSign_s_Obj = NULL;

    if (!root_obj || !msg_obj) {
        goto cleanup;
    }

    cJSON_AddItemToObject(root_obj, "message", msg_obj);
    if (DATA_CHANNEL_ENV_SENSOR & channel) {
        if (iotex_bme680_get_sensor_data(&env_sensor)) {
            goto cleanup;
        }
    }

    if (DATA_CHANNEL_ACTION_SENSOR & channel) {
        if (iotex_icm42605_get_sensor_data(&action_sensor)) {
            goto cleanup;
        }
    }

    /* Snr */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_SNR)) {
        snr = cJSON_CreateNumber(iotex_model_get_signal_quality());
        if (!snr || json_add_obj(msg_obj, "snr", snr)) {
            goto cleanup;
        }
    }

    /* Vbat */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_VBAT)) {
        vbat = cJSON_CreateNumber(iotex_modem_get_battery_voltage());
        if (!vbat || json_add_obj(msg_obj, "vbat", vbat)) {
            goto cleanup;
        }
    }

    /* TODO GPS */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GPS)) {
        int i = getGPS(&latitude,&longitude);
        if (!i) {
            lat = cJSON_CreateNumber(latitude);
            if (!lat || json_add_obj(msg_obj, "latitude", lat)) {
                goto cleanup;
            }    
            lon = cJSON_CreateNumber(longitude);
            if (!lon || json_add_obj(msg_obj, "longitude", lon)) {
                goto cleanup;
            } 
        } else {
            lat = cJSON_CreateNumber(20000.00);
            if (!lat || json_add_obj(msg_obj, "latitude", lat)) {
                goto cleanup;
            }    
            lon = cJSON_CreateNumber(20000.00);
            if (!lon || json_add_obj(msg_obj, "longitude", lon)) {
                goto cleanup;
            }             
        }                    
    }

    /* Env sensor gas */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GAS)) {
        gas_resistance = cJSON_CreateNumber(env_sensor.gas_resistance);
        if (!gas_resistance || json_add_obj(msg_obj, "gasResistance", gas_resistance)) {
            goto cleanup;
        }
    }

    /* Env sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP)) {
        temperature = cJSON_CreateNumber(env_sensor.temperature);
        if (!temperature || json_add_obj(msg_obj, "temperature", temperature)) {
            goto cleanup;
        }
    }

    /* Env sensor pressure */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_PRESSURE)) {
        pressure = cJSON_CreateNumber(env_sensor.pressure);
        if (!pressure || json_add_obj(msg_obj, "pressure", pressure)) {
            goto cleanup;
        }

    }

    /* Env sensor humidity */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_HUMIDITY)) {
        humidity = cJSON_CreateNumber(env_sensor.humidity);
        if (!humidity || json_add_obj(msg_obj, "humidity", humidity)) {
            goto cleanup;
        }
    }

    /* Env sensor light */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_LIGHT_SENSOR)) {
        AmbientLight=iotex_Tsl2572ReadAmbientLight();
        light = cJSON_CreateNumber(AmbientLight);
        if (!light || json_add_obj(msg_obj, "light", light)) {
            goto cleanup;
        }
    }    

    /* Action sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP2)) {
        temperature2 = cJSON_CreateNumber(action_sensor.temperature);
        if (!temperature2 || json_add_obj(msg_obj, "temperature2", temperature2)) {
            goto cleanup;
        }
    }

    /* Action sensor gyroscope data */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GYROSCOPE)) {
        int gyroscope[ARRAY_SIZE(action_sensor.gyroscope)];

        for (i = 0; i < ARRAY_SIZE(action_sensor.gyroscope); i++) {
            gyroscope[i] = action_sensor.gyroscope[i];
        }

        gyroscope_obj = cJSON_CreateIntArray(gyroscope, ARRAY_SIZE(gyroscope));
        if (!gyroscope_obj || json_add_obj(msg_obj, "gyroscope", gyroscope_obj)) {
            goto cleanup;
        }
    }

    /* Action sensor accelerometer */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_ACCELEROMETER)) {
        int accelerometer[ARRAY_SIZE(action_sensor.accelerometer)];

        for (i = 0; i < ARRAY_SIZE(action_sensor.accelerometer); i++) {
            accelerometer[i] = action_sensor.accelerometer[i];
        }

        accelerometer_obj = cJSON_CreateIntArray(accelerometer, ARRAY_SIZE(accelerometer));
        if (!accelerometer_obj || json_add_obj(msg_obj, "accelerometer", accelerometer_obj)) {
            goto cleanup;
        }
    }

    /* Add timestamp */
    if (json_add_str(msg_obj, "timestamp", iotex_modem_get_clock(NULL))) {
        goto cleanup;
    }

    /*  get random number */
    __disable_irq();
    GenRandom(random);
    __enable_irq();
    random[sizeof(random)-1] = 0;
    random_obj = cJSON_CreateString(random); 
    if (!random_obj || json_add_obj(msg_obj, "random", random_obj)) {
        goto cleanup;
    }

    /*  ecc public key */
    if (get_ecc_public_key(NULL)) {
        get_ecc_public_key(jsStr);
        jsStr[128] = 0;
        ecc_pub_obj = cJSON_CreateString(jsStr);
        if (!ecc_pub_obj || json_add_obj(msg_obj, "eccPubkey", ecc_pub_obj)) {
            goto cleanup;
        }        
    }

    output->buf = cJSON_PrintUnformatted(root_obj);            
    doESDA_sep256r_Sign(output->buf, strlen(output->buf), esdaSign, &sinLen);   
    hex2str(esdaSign, sinLen, jsStr);
    cJSON_free(output->buf);
    memcpy(esdaSign, jsStr, 64);
    esdaSign[64] = 0;
    sign_obj = cJSON_CreateObject();
    if (!sign_obj)
        goto cleanup;

    cJSON_AddItemToObject(root_obj, "signature", sign_obj);  
    esdaSign_r_Obj = cJSON_CreateString(esdaSign);
    if (!esdaSign_r_Obj || json_add_obj(sign_obj, "r", esdaSign_r_Obj))
        goto cleanup;

    esdaSign_s_Obj = cJSON_CreateString(jsStr + 64);
    if (!esdaSign_s_Obj || json_add_obj(sign_obj, "s", esdaSign_s_Obj))
        goto cleanup;

    output->buf = cJSON_PrintUnformatted(root_obj);
    output->len = strlen(output->buf);
    LOG_INFO("json package: %d bytes\n", output->len);
    ret = 0;

cleanup:
    if (root_obj) cJSON_Delete(root_obj);
    if (msg_obj) cJSON_Delete(msg_obj);
    if (snr) cJSON_Delete(snr);
    if (vbat) cJSON_Delete(vbat);
    if (lat) cJSON_Delete(lat);
    if (lon) cJSON_Delete(lon);
    if (gas_resistance) cJSON_Delete(gas_resistance);
    if (temperature) cJSON_Delete(temperature);
    if (pressure) cJSON_Delete(pressure);
    if (humidity) cJSON_Delete(humidity);
    if (light) cJSON_Delete(light);
    if (temperature2) cJSON_Delete(temperature2);
    if (gyroscope_obj) cJSON_Delete(gyroscope_obj);
    if (accelerometer_obj) cJSON_Delete(accelerometer_obj);
    if (random_obj) cJSON_Delete(random_obj);
    if (ecc_pub_obj) cJSON_Delete(ecc_pub_obj);
    if (sign_obj) cJSON_Delete(sign_obj);
    if (esdaSign_r_Obj) cJSON_Delete(esdaSign_r_Obj);
    if (esdaSign_s_Obj) cJSON_Delete(esdaSign_s_Obj);
    return ret;
}

int iotex_mqtt_bin_to_json(uint8_t *buffer, uint16_t channel, struct mqtt_payload *output)
{    
    char epoch_buf[TIMESTAMP_STR_LEN];
    uint32_t write_cnt = 0;
    double  timestamp;
    float  tmp;
    int i;
    int ret = -ENOMEM;
    char esdaSign[65];
    char jsStr[130];
    int  sinLen;
    char random[17];
    uint16_t vol_Integer;
    cJSON *root_obj = cJSON_CreateObject();
    cJSON *msg_obj = cJSON_CreateObject();
    cJSON *sign_obj = cJSON_CreateObject();
    cJSON *snr = NULL;
    cJSON *vbat = NULL;
    cJSON *lat = NULL;
    cJSON *lon = NULL;
    cJSON *gas_resistance = NULL;
    cJSON *temperature = NULL;
    cJSON *pressure = NULL;
    cJSON *humidity = NULL;
    cJSON *light = NULL;
    cJSON *temperature2 = NULL;
    cJSON *gyroscope_obj = NULL;
    cJSON *accelerometer_obj = NULL;
    cJSON *random_obj = NULL;
    cJSON *esdaSign_r_Obj = NULL;
    cJSON *esdaSign_s_Obj = NULL;

    if (!root_obj || !msg_obj || !sign_obj) {
        goto cleanup;
    }

    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_SNR)) {
        snr = cJSON_CreateNumber(buffer[write_cnt++]);
        if (!snr || json_add_obj(msg_obj, "SNR", snr)) {
            goto cleanup;
        }
    }

    /* Vbatx100 */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_VBAT)) {
        vol_Integer = (uint16_t)(buffer[write_cnt++] | (buffer[write_cnt++] << 8));
        vbat = cJSON_CreateNumber((double)(vol_Integer/1000.0));
        if (!vbat || json_add_obj(msg_obj, "VBAT", vbat)) {
            goto cleanup;
        }
    }

    /* TODO GPS */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GPS)) {
        char *pDat = &latitude;
        int i;
        char j;
        for (i = 0; i < sizeof(latitude); i++) {
            *pDat++ = buffer[write_cnt++];
        }
        pDat = &longitude;
        for (i = 0,j = 0xFF; i < sizeof(longitude); i++) {
            *pDat = buffer[write_cnt++];
            j &= *pDat;
            pDat++;
        }
        if (j != 0xFF) {
            lat = cJSON_CreateNumber(latitude);
            if (!lat || json_add_obj(msg_obj, "latitude", lat)) {
                goto cleanup;
            }
            lon = cJSON_CreateNumber(longitude);
            if (!lon || json_add_obj(msg_obj, "longitude", lon)) {
                goto cleanup;
            }
        } else {
            lat = cJSON_CreateNumber(20000.00);
            if (!lat || json_add_obj(msg_obj, "latitude", lat)) {
                goto cleanup;
            }
            lon = cJSON_CreateNumber(20000.00);
            if (!lon || json_add_obj(msg_obj, "longitude", lon)) {
                goto cleanup;
            }
        }
    }

    /* Env sensor gas */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GAS)) {
        memcpy(&tmp, buffer + write_cnt, sizeof(tmp));
        write_cnt += sizeof(tmp);
        gas_resistance = cJSON_CreateNumber(tmp);
        if (!gas_resistance || json_add_obj(msg_obj, "gasResistance", gas_resistance)) {
            goto cleanup;
        }
    }

    /* Env sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP)) {
        memcpy(&tmp, buffer + write_cnt, sizeof(tmp));
        write_cnt += sizeof(tmp);
        temperature = cJSON_CreateNumber(tmp);
        if (!temperature || json_add_obj(msg_obj, "temperature", temperature)) {
            goto cleanup;
        }
    }

    /* Env sensor pressure */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_PRESSURE)) {
        memcpy(&tmp, buffer + write_cnt, sizeof(tmp));
        write_cnt += sizeof(tmp);
        pressure = cJSON_CreateNumber(tmp);
        if (!pressure || json_add_obj(msg_obj, "pressure", pressure)) {
            goto cleanup;
        }
    }

    /* Env sensor humidity */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_HUMIDITY)) {
        memcpy(&tmp, buffer + write_cnt, sizeof(tmp));
        write_cnt += sizeof(tmp);
        humidity = cJSON_CreateNumber(tmp);
        if (!humidity || json_add_obj(msg_obj, "humidity", humidity)) {
            goto cleanup;
        }
    }

    /* Env sensor light */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_HUMIDITY)) {
        memcpy(&tmp, buffer + write_cnt, sizeof(tmp));
        write_cnt += sizeof(tmp);
        light = cJSON_CreateNumber(tmp);
        if (!light || json_add_obj(msg_obj, "light", light)) {
            goto cleanup;
        }
    }

    /* Action sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP2)) {
        memcpy(&tmp, buffer + write_cnt, sizeof(tmp));
        write_cnt += sizeof(tmp);
        temperature2 = cJSON_CreateNumber(tmp);
        if (!temperature2 || json_add_obj(msg_obj, "temperature2", temperature2)) {
            goto cleanup;
        }
    }

    /* Action sensor gyroscope data */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GYROSCOPE)) {
        int gyroscope[3];
        int16_t buf[3];
        memcpy(buf, buffer + write_cnt, sizeof(buf));
        write_cnt += sizeof(buf);
        for (i = 0; i < ARRAY_SIZE(gyroscope); i++) {
            gyroscope[i] = buf[i];
        }
        gyroscope_obj = cJSON_CreateIntArray(gyroscope, ARRAY_SIZE(gyroscope));
        if (!gyroscope_obj || json_add_obj(msg_obj, "gyroscope", gyroscope_obj)) {
            goto cleanup;
        }
    }

    /* Action sensor accelerometer */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_ACCELEROMETER)) {
        int accelerometer[3];
        int16_t buf[3];
        memcpy(buf, buffer + write_cnt, sizeof(buf));
        write_cnt += sizeof(buf);
        for (i = 0; i < ARRAY_SIZE(accelerometer); i++) {
            accelerometer[i] = buf[i];
        }
        accelerometer_obj = cJSON_CreateIntArray(accelerometer, ARRAY_SIZE(accelerometer));
        if (!accelerometer_obj || json_add_obj(msg_obj, "accelerometer", accelerometer_obj)) {
            goto cleanup;
        }
    }

    /* Add timestamp */
    memcpy(&timestamp, buffer + write_cnt, sizeof(timestamp));
    if (json_add_str(msg_obj, "timestamp", epoch_buf)) {
        goto cleanup;
    }
    /*  get random number */
    __disable_irq();
    GenRandom(random);
    __enable_irq();
    random[sizeof(random) - 1] = 0;
    random_obj = cJSON_CreateString(random); 
    if (!random_obj  || json_add_obj(msg_obj, "random", random_obj)) {
        goto cleanup;
    }

    cJSON_AddItemToObject(root_obj, "message", msg_obj);
    output->buf = cJSON_PrintUnformatted(msg_obj);
    doESDA_sep256r_Sign(output->buf, strlen(output->buf), esdaSign,&sinLen);   
    hex2str(esdaSign, sinLen,jsStr);
    free(output->buf);
    memcpy(esdaSign, jsStr, 64);
    esdaSign[64]= 0;
    
    esdaSign_r_Obj = cJSON_CreateString(esdaSign); 
    if (!esdaSign_r_Obj  || json_add_obj(sign_obj, "r", esdaSign_r_Obj))
        goto cleanup;

    esdaSign_s_Obj = cJSON_CreateString(jsStr + 64);
    if(!esdaSign_s_Obj  || json_add_obj(sign_obj, "s", esdaSign_s_Obj))
        goto cleanup;

    cJSON_AddItemToObject(root_obj, "signature", sign_obj);
    output->buf = cJSON_PrintUnformatted(root_obj);
    output->len = strlen(output->buf);

    ret = 0;

cleanup:
    if (root_obj) cJSON_Delete(root_obj);
    if (msg_obj) cJSON_Delete(msg_obj);
    if (sign_obj) cJSON_Delete(sign_obj);
    if (snr) cJSON_Delete(snr);
    if (vbat) cJSON_Delete(vbat);
    if (lat) cJSON_Delete(lat);
    if (lon) cJSON_Delete(lon);
    if (gas_resistance) cJSON_Delete(gas_resistance);
    if (temperature) cJSON_Delete(temperature);
    if (pressure) cJSON_Delete(pressure);
    if (humidity) cJSON_Delete(humidity);
    if (light) cJSON_Delete(light);
    if (temperature2) cJSON_Delete(temperature2);
    if (gyroscope_obj) cJSON_Delete(gyroscope_obj);
    if (accelerometer_obj) cJSON_Delete(accelerometer_obj);
    if (random_obj) cJSON_Delete(random_obj);
    if (esdaSign_r_Obj) cJSON_Delete(esdaSign_r_Obj);
    if (esdaSign_s_Obj) cJSON_Delete(esdaSign_s_Obj);

    return ret;
}
/*  protoc --nanopb_out=.  package.proto */

int SensorPackage(uint16_t channel, uint8_t *buffer)
{
    int i;
    char esdaSign[65];
    int sinLen;
    uint32_t uint_timestamp, startTime;
    BinPackage binpack = BinPackage_init_zero;
    PedometerData pedometerdat = PedometerData_init_zero;
    /*  Initialize buffer */
    memset(buffer, 0, DATA_BUFFER_SIZE);

    i = getGPS(&latitude,&longitude);
    if (!i) {
        pedometerdat.latitude = (int32_t)(latitude * 10000000);
        pedometerdat.has_latitude = true;
        pedometerdat.longitude = (int32_t)(longitude * 10000000);
        pedometerdat.has_longitude = true;
    } else {
        pedometerdat.latitude = 2000000000;
        pedometerdat.has_latitude = true;
        pedometerdat.longitude = 2000000000;
        pedometerdat.has_longitude = true;
    }
    pedometerdat.type = readPedometer(&pedometerdat.pace, &pedometerdat.steps, &startTime);

    /* Add timestamp */
    uint_timestamp = atoi(iotex_modem_get_clock(NULL));
    /*  get random number */
    __disable_irq();
    GenRandom(pedometerdat.random);
    __enable_irq();
    pedometerdat.random[sizeof(pedometerdat.random) - 1] = 0;

    pb_ostream_t enc_datastream;
    enc_datastream = pb_ostream_from_buffer(binpack.data.bytes, sizeof(binpack.data.bytes));
    if (!pb_encode(&enc_datastream, PedometerData_fields, &pedometerdat)) {
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_datastream));
        return 0;
    }

    binpack.data.size = enc_datastream.bytes_written;
    binpack.data.bytes[enc_datastream.bytes_written] = (char)((uint_timestamp & 0xFF000000) >> 24);
    binpack.data.bytes[enc_datastream.bytes_written + 1] = (char)((uint_timestamp & 0x00FF0000) >> 16);
    binpack.data.bytes[enc_datastream.bytes_written + 2] = (char)((uint_timestamp & 0x0000FF00) >> 8);
    binpack.data.bytes[enc_datastream.bytes_written + 3] = (char)(uint_timestamp & 0x000000FF);
    buffer[0] = (char)((BinPackage_PackageType_PEDOMETER & 0xFF000000) >> 24);
    buffer[1] = (char)((BinPackage_PackageType_PEDOMETER & 0x00FF0000) >> 16);
    buffer[2] = (char)((BinPackage_PackageType_PEDOMETER & 0x0000FF00) >> 8);
    buffer[3] = (char)(BinPackage_PackageType_PEDOMETER & 0x000000FF);
    memcpy(buffer + 4,  binpack.data.bytes, enc_datastream.bytes_written + 4);
    LOG_INF("uint_timestamp:%d \n",uint_timestamp);
    doESDASign(buffer, enc_datastream.bytes_written + 8, esdaSign, &sinLen);
    LOG_INF("sinLen:%d\n", sinLen);
    memcpy(binpack.signature, esdaSign, 64);
    binpack.timestamp = uint_timestamp;
    binpack.type = BinPackage_PackageType_PEDOMETER;
    pb_ostream_t enc_packstream;
    enc_packstream = pb_ostream_from_buffer(buffer, DATA_BUFFER_SIZE);
    if (!pb_encode(&enc_packstream, BinPackage_fields, &binpack)) {
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_packstream));
        return 0;
    }
    return enc_packstream.bytes_written;
}