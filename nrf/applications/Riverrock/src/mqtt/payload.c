#include <stdio.h>
#include <zephyr/logging/log.h>
#include "cJSON.h"
#include "cJSON_os.h"
#include "mqtt.h"
#include "config.h"
#include "hal/hal_adc.h"
#include "nvs/local_storage.h"
#include "modem/modem_helper.h"
#include "bme/bme680_helper.h"
#include "icm/icm_chip_helper.h"
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

bool iotex_mqtt_sampling_data_and_store(uint16_t channel) {
    uint8_t buffer[128];
    uint32_t write_cnt = 0;
    iotex_storage_bme680 env_sensor;
    iotex_storage_icm action_sensor;
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
        if (iotex_icm_get_sensor_data(&action_sensor)) {
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
/*  protoc --nanopb_out=.  package.proto */

int SensorPackage(uint16_t channel, uint8_t *buffer)
{
    int i;
    iotex_storage_bme680 env_sensor;
    iotex_storage_icm action_sensor;
    char esdaSign[65];
    char jsStr[130];
    int sinLen;
    float AmbientLight = 0.0;
    uint32_t uint_timestamp;
    /* char random[17]; */
    BinPackage binpack = BinPackage_init_zero;
    SensorData sensordat = SensorData_init_zero;

    /*  Initialize buffer */
    memset(buffer, 0, DATA_BUFFER_SIZE);
    if (DATA_CHANNEL_ENV_SENSOR & channel) {
        if (iotex_bme680_get_sensor_data(&env_sensor)) {
            return 0;
        }
    }
    if (DATA_CHANNEL_ACTION_SENSOR & channel) {
        if (iotex_icm_get_sensor_data(&action_sensor)) {
            return 0;
        }
    }

    /* Snr */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_SNR)) {
        sensordat.snr = (uint32_t)(iotex_model_get_signal_quality() * 100);
        LOG_INF("SNR:%d\n", sensordat.snr);
        sensordat.has_snr=true;
    }

    /* Vbat */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_VBAT)) {
        sensordat.vbat = getDevVol()/10;
        sensordat.has_vbat = true;
    }

    /* GPS */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GPS)) {
        int i = getGPS(&latitude,&longitude);
        if (!i) {
            sensordat.latitude = (int32_t)(latitude * 10000000);
            sensordat.has_latitude = true;
            sensordat.longitude = (int32_t)(longitude * 10000000);
            sensordat.has_longitude = true;
        } else {
            sensordat.latitude = 2000000000;
            sensordat.has_latitude = true;
            sensordat.longitude = 2000000000;
            sensordat.has_longitude = true;
        }
    }

    /* Env sensor gas */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GAS)) {
        sensordat.gasResistance = (uint32_t)(env_sensor.gas_resistance * 100);
        sensordat.has_gasResistance = true;
    }

    /* Env sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP)) {
        sensordat.temperature = (int32_t)(env_sensor.temperature * 100);
        sensordat.has_temperature = true;
    }

    /* Env sensor pressure */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_PRESSURE)) {
        sensordat.pressure = (uint32_t)(env_sensor.pressure * 100);
        sensordat.has_pressure = true;
    }

    /* Env sensor humidity */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_HUMIDITY)) {
        sensordat.humidity = (uint32_t)(env_sensor.humidity * 100);
        sensordat.has_humidity = true;
    }

    /* Env sensor light */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_LIGHT_SENSOR)) {
        AmbientLight = iotex_Tsl2572ReadAmbientLight();
        sensordat.light = (uint32_t)(AmbientLight * 100);
        sensordat.has_light = true;
    }

    /* Action sensor temperature */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_TEMP2)) {
        sensordat.temperature2 = sensordat.temperature/*(uint32_t)(action_sensor.temperature * 100)*/;
        sensordat.has_temperature2 = true;      
    }

    /* Action sensor gyroscope data */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_GYROSCOPE)) {
        for (i = 0; i < ARRAY_SIZE(action_sensor.gyroscope); i++) {
            sensordat.gyroscope[i] = (int32_t)(action_sensor.gyroscope[i]);
        }
    }

    /* Action sensor accelerometer */
    if (IOTEX_DATA_CHANNEL_IS_SET(channel, DATA_CHANNEL_ACCELEROMETER)) {
        for (i = 0; i < ARRAY_SIZE(action_sensor.accelerometer); i++) {
            sensordat.accelerometer[i] = (int32_t)(action_sensor.accelerometer[i]);
        }
    }

    /* Add timestamp */
    uint_timestamp = getSysTimestamp_s();
    /*  get random number */
    __disable_irq();
    GenRandom(sensordat.random);
    __enable_irq();
    sensordat.random[sizeof(sensordat.random) - 1] = 0;
    sensordat.has_random = true;

    pb_ostream_t enc_datastream;
    enc_datastream = pb_ostream_from_buffer(binpack.data.bytes, sizeof(binpack.data.bytes));
    if (!pb_encode(&enc_datastream, SensorData_fields, &sensordat)) {
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_datastream));
        return 0;
    }

    binpack.data.size = enc_datastream.bytes_written;
    binpack.data.bytes[enc_datastream.bytes_written] = (char)((uint_timestamp & 0xFF000000) >> 24);
    binpack.data.bytes[enc_datastream.bytes_written + 1] = (char)((uint_timestamp & 0x00FF0000) >> 16);
    binpack.data.bytes[enc_datastream.bytes_written + 2] = (char)((uint_timestamp & 0x0000FF00) >> 8);
    binpack.data.bytes[enc_datastream.bytes_written + 3] = (char)(uint_timestamp & 0x000000FF);
    *(uint32_t*)buffer = BinPackage_PackageType_DATA;
    memcpy(buffer + 4,  binpack.data.bytes, enc_datastream.bytes_written + 4);
    LOG_INF("uint_timestamp:%d \n",uint_timestamp);
    doESDASign(buffer, enc_datastream.bytes_written + 8, esdaSign, &sinLen);
    LOG_INF("sinLen:%d\n", sinLen);
    memcpy(binpack.signature, esdaSign, 64);
    binpack.timestamp = uint_timestamp;
    binpack.type = BinPackage_PackageType_DATA;
    pb_ostream_t enc_packstream;
    enc_packstream = pb_ostream_from_buffer(buffer, DATA_BUFFER_SIZE);
    if (!pb_encode(&enc_packstream, BinPackage_fields, &binpack)) {
        LOG_ERR("pb encode error in %s [%s]\n", __func__,PB_GET_ERROR(&enc_packstream));
        return 0;
    }
    LOG_INF("sen->snr:%d\n", sensordat.snr);
    return enc_packstream.bytes_written;
}
