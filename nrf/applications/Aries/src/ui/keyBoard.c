#include <zephyr.h>
#include <drivers/gpio.h>
#include <logging/log.h>
#include "hal/hal_gpio.h"
#include "keyBoard.h"
#include "mqtt/devReg.h"
#include "display.h"

LOG_MODULE_REGISTER(keyBoard, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define GPIO_DIR_OUT  GPIO_OUTPUT
#define GPIO_DIR_IN   GPIO_INPUT
#define GPIO_INT  GPIO_INT_ENABLE
#define GPIO_INT_DOUBLE_EDGE  GPIO_INT_EDGE_BOTH
#define gpio_pin_write  gpio_pin_set


static struct gpio_callback up_key_gpio_cb, down_key_gpio_cb, pwr_key_gpio_cb;
static uint8_t pressedKey,combinationKeys=KB_NO_KEY;
static struct k_delayed_work power_off_button_work;
static bool b_poweroff = false; /* qhm add 0830*/


extern struct device *__gpio0_dev;

void ClearKey(void) {
    pressedKey = KB_NO_KEY;
}

uint8_t getKey(void) {
    return pressedKey;
}

bool IsEnterPressed(void) {
    return (pressedKey & KB_POWER_KEY);
}

bool IsUpPressed(void) {
    return (pressedKey & KB_UP_KEY);
}

bool IsDownPressed(void) {
    return (pressedKey & KB_DOWN_KEY);
}

bool isUpKeyStartupPressed(void) {
    u32_t key = gpio_pin_get(__gpio0_dev, IO_UP_KEY);
    return key == 0;
}

bool isDownKeyStartupPressed(void) {
    u32_t key = gpio_pin_get(__gpio0_dev, IO_DOWN_KEY);
    return key == 0;
}

bool isComninationKeys(enum USER_KEY_DEF combination_keys) {
    return ((combinationKeys & combination_keys) == combination_keys);
}

static void up_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {
    u32_t key = gpio_pin_get(port, IO_UP_KEY);
    if (key > 0) {
        pressedKey |= KB_UP_KEY;
        key = gpio_pin_get(port, POWER_KEY);
        if (key > 0) {
            k_delayed_work_cancel(&power_off_button_work);
        }
        combinationKeys &= ~KB_UP_KEY;
        LOG_DBG("up_key_release\n");
    } else {
        LOG_DBG("up_key_press\n");
        combinationKeys |= KB_UP_KEY;
    }
}

static void down_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {
    u32_t key = gpio_pin_get(port, IO_DOWN_KEY);
    if (key > 0) {
        pressedKey |= KB_DOWN_KEY;
        combinationKeys &= ~KB_DOWN_KEY;
        LOG_DBG("down_key_release\n");
    } else {
        LOG_DBG("down_key_press\n");
        combinationKeys |= KB_DOWN_KEY;
    }
}

static void pwr_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {
    u32_t pwr_key = gpio_pin_get(port, POWER_KEY);
    if (0 == pwr_key) {
        pressedKey |= KB_POWER_KEY;
        combinationKeys |= KB_POWER_KEY;
        k_delayed_work_submit(&power_off_button_work,K_SECONDS(5));
        LOG_DBG("Power key pressed\n");
    } else {
        k_delayed_work_cancel(&power_off_button_work);
        if (b_poweroff) {
            gpio_pin_write(__gpio0_dev, IO_POWER_ON, POWER_OFF);
            b_poweroff = false;
        }
        combinationKeys &= ~KB_POWER_KEY;
        /*end of qhm add 0830*/
        LOG_DBG("Power key released\n");
    }
}

static void power_off_handler(struct k_work *work) {
    b_poweroff = true;     /* qhm add 0830*/
    gpio_poweroffNotice();
}

void iotex_key_init(void) {
    ClearKey();
    /* up_key pin */
    gpio_pin_configure(__gpio0_dev, IO_UP_KEY,
                        (GPIO_DIR_IN | GPIO_INT | GPIO_PULL_UP |
                        GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                        GPIO_INT_DEBOUNCE));
    gpio_init_callback(&up_key_gpio_cb, up_key_callback, BIT(IO_UP_KEY));
    gpio_add_callback(__gpio0_dev, &up_key_gpio_cb);
    /* gpio_pin_enable_callback(__gpio0_dev, IO_UP_KEY); */
    gpio_pin_interrupt_configure(__gpio0_dev, IO_UP_KEY,GPIO_INT_EDGE_BOTH);
    /* down_key pin */
    gpio_pin_configure(__gpio0_dev, IO_DOWN_KEY,
                        (GPIO_DIR_IN | GPIO_INT | GPIO_PULL_UP |
                        GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                        GPIO_INT_DEBOUNCE));
    gpio_init_callback(&down_key_gpio_cb, down_key_callback, BIT(IO_DOWN_KEY));
    gpio_add_callback(__gpio0_dev, &down_key_gpio_cb);
    /* gpio_pin_enable_callback(__gpio0_dev, IO_DOWN_KEY); */
    gpio_pin_interrupt_configure(__gpio0_dev, IO_DOWN_KEY,GPIO_INT_EDGE_BOTH);
    /* Power key pin configure */
    k_delayed_work_init(&power_off_button_work, power_off_handler);
    gpio_pin_configure(__gpio0_dev, POWER_KEY,
                        (GPIO_DIR_IN | GPIO_INT |
                        GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                        GPIO_INT_DEBOUNCE));
    gpio_init_callback(&pwr_key_gpio_cb, pwr_key_callback, BIT(POWER_KEY));
    gpio_add_callback(__gpio0_dev, &pwr_key_gpio_cb);
    /* gpio_pin_enable_callback(__gpio0_dev, POWER_KEY); */
    gpio_pin_interrupt_configure(__gpio0_dev, POWER_KEY,GPIO_INT_EDGE_BOTH);
}