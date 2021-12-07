#include <drivers/adc.h>
#include <stdio.h>
#include <zephyr.h>
#include <logging/log.h>
#include "hal_adc.h"

LOG_MODULE_REGISTER(hal_adc, CONFIG_ASSET_TRACKER_LOG_LEVEL);

static struct device *__adc_dev;

/* #define CONFIG_DEBUG_ADC  */

int iotex_hal_adc_sample(void) {
    int ret;
    int adc_voltage = 0;
    int16_t sample_buffer;
    const struct adc_sequence sequence = {
        .channels = BIT(ADC_1ST_CHANNEL_ID),
        .buffer = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution = ADC_RESOLUTION,
        .oversampling = 0,

    };

    if (!__adc_dev) {
        return -1;
    }
    ret = adc_read(__adc_dev, &sequence);
    if (ret || sample_buffer <= 0) {
        return 0;
    }
    /* adc_voltage = sample_buffer / 1023.0 * 2 * 3600.0 / 1000.0; */
    adc_voltage = (sample_buffer * 7200) / 16384 ;
     /*   adc_voltage = (sample_buffer * 4500) / 1024;  */
#ifdef CONFIG_DEBUG_ADC
    LOG_INF("ADC raw value: %d\n", sample_buffer);
    fprintf(stdout, "Measured voltage: %d mV\n", adc_voltage);
#endif
    return adc_voltage;
}

int iotex_hal_adc_init(void) {
    int err;
    const struct adc_channel_cfg m_1st_channel_cfg = {
        .gain = ADC_GAIN,
        .reference = ADC_REFERENCE,
        .acquisition_time = ADC_ACQUISITION_TIME,
        .channel_id = ADC_1ST_CHANNEL_ID,
        .input_positive = ADC_1ST_CHANNEL_INPUT,
        .differential = 0,
    };

    __adc_dev = device_get_binding("ADC_0");
    if (!__adc_dev) {
        LOG_ERR("device_get_binding ADC_0 failed\n");
    }
    err = adc_channel_setup(__adc_dev, &m_1st_channel_cfg);
    if (err) {
        LOG_ERR("Error in adc setup: %d\n", err);
    }
    NRF_SAADC_NS->TASKS_CALIBRATEOFFSET = 1;
    iotex_hal_adc_sample();
    return err;
}