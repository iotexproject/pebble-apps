/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <zephyr.h>
#include <drivers/pwm.h>
#include <string.h>
#include "ui.h"
#include "led_pwm.h"
#include "led_effect.h"
#include <logging/log.h>

LOG_MODULE_REGISTER(ui_led_pwm, CONFIG_UI_LOG_LEVEL);

struct device *beep_pwm_dev = NULL;

int onBeepMePressed(int ms) {
    int err = 0;
    if (!beep_pwm_dev) {
        beep_pwm_dev = device_get_binding("PWM_1");
    }
    pwm_pin_set_usec(beep_pwm_dev, 11, 370, 185, 0);/* 2.7kHz ==370us  185/370=50% duty */
    k_sleep(K_MSEC(ms));  /* 1.5S delay */
    pwm_pin_set_usec(beep_pwm_dev, 11, 0, 0, 0);

    return err;
}
