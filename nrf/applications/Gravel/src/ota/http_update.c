/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <zephyr.h>
#include <drivers/gpio.h>
#include <drivers/flash.h>
#include <bsd.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/bsdlib.h>
#include <modem/modem_key_mgmt.h>
#include <net/fota_download.h>
#include <dfu/mcuboot.h>
#include <logging/log.h>
#include "keyBoard.h"
#include "display.h"
#include "mqtt/devReg.h"

LOG_MODULE_REGISTER(http_update, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define  LED_PORT     "GPIO_0"
#define TLS_SEC_TAG   42

#define LED_GREEN     26    /* p0.00 == LED_GREEN  0=on 1=off */
#define LED_BLUE      27    /* p0.01 == LED_BLUE   0=on 1=off */
#define LED_RED       30    /* p0.02 == LED_RED    0=on 1=off */

#define LED_ON        0
#define LED_OFF       1

#define GPIO_DIR_OUT  GPIO_OUTPUT
#define GPIO_DIR_IN   GPIO_INPUT
#define GPIO_INT  GPIO_INT_ENABLE
#define GPIO_INT_DOUBLE_EDGE  GPIO_INT_EDGE_BOTH
#define gpio_pin_write  gpio_pin_set

static const struct device *gpiob;
static struct gpio_callback gpio_cb;
static struct k_work fota_work;
static struct k_delayed_work fota_status_check;
static int aliveCnt = 0;
static uint8_t otaHost[100];
static uint8_t otaFile[200];
static int otaProgress = 0;
const struct device *dev;

int getOTAProgress(void) {
    return otaProgress;
}

/**@brief Start transfer of the file. */
static void app_dfu_transfer_start(struct k_work *unused)
{
    int retval;
    int sec_tag;
    char *apn = NULL;

    LOG_INF("app_dfu_transfer_start\n ");
#ifndef CONFIG_USE_HTTPS
    sec_tag = -1;
#else
    sec_tag = TLS_SEC_TAG;
#endif
    retval = fota_download_start(otaHost, otaFile, sec_tag, apn, 0);
    if (retval != 0) {
        /* Re-enable button callback */
        LOG_ERR("fota_download_start() failed, err %d\n", retval);
    }
    LOG_INF("fota_download_start over\n");
}

static void fota_status(struct k_work *unused)
{
    aliveCnt++;
    if (aliveCnt > 12){
        aliveCnt = 0;
        LOG_ERR("Received hangup from fota_download\n");
        k_work_submit(&fota_work);
        k_delayed_work_submit(&fota_status_check, K_MSEC(2000));
    } else {
        k_delayed_work_submit(&fota_status_check, K_MSEC(10000));
    }
    LOG_INF("aliveCnt:%d\n", aliveCnt);
}

/**@brief Turn on LED0 and LED1 if CONFIG_APPLICATION_VERSION
 * is 2 and LED0 otherwise.
 */
static int led_app_version(void) {
    dev = device_get_binding(LED_PORT);
    if (!dev) {
        LOG_ERR("Nordic nRF GPIO driver was not found!\n");
        return 1;
    }
    gpio_pin_configure(dev, LED_GREEN, GPIO_DIR_OUT);    /* p0.00 == LED_GREEN */
    gpio_pin_configure(dev, LED_BLUE, GPIO_DIR_OUT);    /* p0.01 == LED_BLUE */
    gpio_pin_configure(dev, LED_RED, GPIO_DIR_OUT);     /* p0.02 == LED_RED */

    gpio_pin_write(dev, LED_GREEN, LED_ON);    /* p0.00 == LED_GREEN ON */
    gpio_pin_write(dev, LED_BLUE, LED_OFF);    /* p0.00 == LED_BLUE OFF */
    gpio_pin_write(dev, LED_RED, LED_ON);

#if CONFIG_APPLICATION_VERSION == 2
    gpio_pin_write(dev, LED_RED, LED_ON);
#endif
    return 0;
}

void dfu_button_pressed(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins)
{
    k_work_submit(&fota_work);
    k_delayed_work_submit(&fota_status_check, K_MSEC(2000));
    gpio_pin_interrupt_configure(gpiob, DT_GPIO_PIN(DT_ALIAS(sw0), gpios), GPIO_INT_DISABLE);
}

static int dfu_button_init(void) {
    int err;

    gpio_init_callback(&gpio_cb, dfu_button_pressed, BIT(DT_GPIO_PIN(DT_ALIAS(sw0), gpios)));
    err = gpio_add_callback(gpiob, &gpio_cb);
    if (err == 0) {
        err = gpio_pin_interrupt_configure(gpiob,DT_GPIO_PIN(DT_ALIAS(sw0), gpios),GPIO_INT_EDGE_TO_ACTIVE);
    }
    if (err != 0) {
        LOG_ERR("Unable to configure SW0 GPIO pin!\n");
        return 1;
    }
    return 0;
}

static int initSw0(void) {
    int err;

    gpiob = device_get_binding(DT_GPIO_LABEL(DT_ALIAS(sw0), gpios));
    if (!gpiob) {
        LOG_ERR("Nordic nRF GPIO driver was not found!\n");
        return 1;
    }
    err = gpio_pin_configure(gpiob, DT_GPIO_PIN(DT_ALIAS(sw0), gpios),GPIO_INPUT | DT_GPIO_FLAGS(DT_ALIAS(sw0), gpios));

    return (err != 0) ? -1 : 0;
}

void fota_dl_handler(const struct fota_download_evt *evt)
{
    switch (evt->id) {
    case FOTA_DOWNLOAD_EVT_ERROR:
        LOG_ERR("Received error from fota_download\n");
        break;
        /* Fallthrough */
    case FOTA_DOWNLOAD_EVT_ERASE_PENDING:
        LOG_ERR("Received timeout from fota_download\n");
        break;
    case FOTA_DOWNLOAD_EVT_FINISHED:
        /* Re-enable button callback */
        k_delayed_work_cancel(&fota_status_check);
        devRegSet(DEV_UPGRADE_COMPLETE);
        break;
    case FOTA_DOWNLOAD_EVT_PROGRESS:
        otaProgress = evt->progress;
        aliveCnt = 0;
        break;
    default:
        break;
    }
}

static int application_init(void) {
    int err;
    k_work_init(&fota_work, app_dfu_transfer_start);
    k_delayed_work_init(&fota_status_check, fota_status);
    err = fota_download_init(fota_dl_handler);
    return (err != 0) ? err : 0;
}

static void getHost(uint8_t *url, uint8_t *host, uint8_t *file) {
    sscanf(url, "https://%99[^/]/%99[^\n]", host, file);
    LOG_INF("host:%s, file:%s\n", host, file);
}

void initOTA(void) {
    int err;
    boot_write_img_confirmed();
    err = application_init();
    if (err != 0) {
        LOG_ERR("initOTA err \n");
        return;
    }
}

void startOTA(uint8_t *url) {
    getHost(url,otaHost,otaFile);
    k_work_submit(&fota_work);
    k_delayed_work_submit(&fota_status_check, K_MSEC(2000));
}