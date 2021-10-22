#include <drivers/adc.h>
#include <stdio.h>
#include <zephyr.h>
#include "hal_adc.h"

static struct device *__adc_dev;

//#define CONFIG_DEBUG_ADC 

int iotex_hal_adc_sample(void) {
    int ret;
    //float adc_voltage = 0;
    int adc_voltage = 0;
    int16_t sample_buffer;

    const struct adc_sequence sequence = {
        .channels = BIT(ADC_1ST_CHANNEL_ID),
        .buffer = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution = ADC_RESOLUTION,
    };

    if (!__adc_dev) {
        return -1;
    }

    ret = adc_read(__adc_dev, &sequence);

    if (ret || sample_buffer <= 0) {
        return 0;
    }

    //adc_voltage = sample_buffer / 1023.0 * 2 * 3600.0 / 1000.0;
    adc_voltage = (sample_buffer * 7200) / 1023 ;

#ifdef CONFIG_DEBUG_ADC
    printk("ADC raw value: %d\n", sample_buffer);
    fprintf(stdout, "Measured voltage: %d mV\n", adc_voltage);
#endif
    return adc_voltage;
}

int iotex_hal_adc_init(void) {

    int err;
    __adc_dev = device_get_binding("ADC_0");

    const struct adc_channel_cfg m_1st_channel_cfg = {
        .gain = ADC_GAIN,
        .reference = ADC_REFERENCE,
        .acquisition_time = ADC_ACQUISITION_TIME,
        .channel_id = ADC_1ST_CHANNEL_ID,
        .input_positive = ADC_1ST_CHANNEL_INPUT,
    };

    if (!__adc_dev) {
        printk("device_get_binding ADC_0 failed\n");
    }

    err = adc_channel_setup(__adc_dev, &m_1st_channel_cfg);

    if (err) {
        printk("Error in adc setup: %d\n", err);
    }

    NRF_SAADC_NS->TASKS_CALIBRATEOFFSET = 1;
    return err;
}