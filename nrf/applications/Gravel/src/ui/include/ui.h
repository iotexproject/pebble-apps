/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *
 * @brief   User interface module.
 *
 * Module that handles user interaction through button, RGB LED and buzzer.
 */

#ifndef UI_H__
#define UI_H__

#include <zephyr.h>
#include <dk_buttons_and_leds.h>

#include "led_effect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_BUTTON_1            1
#define UI_BUTTON_2            2
#define UI_SWITCH_1            3
#define UI_SWITCH_2            4

#define UI_LED_1            1
#define UI_LED_2            2
#define UI_LED_3            3
#define UI_LED_4            4

#define UI_NMOS_1            0
#define UI_NMOS_2            1
#define UI_NMOS_3            2
#define UI_NMOS_4            3

#define UI_LED_ON(x)            (x)
#define UI_LED_BLINK(x)            ((x) << 8)
#define UI_LED_GET_ON(x)        ((x) & 0xFF)
#define UI_LED_GET_BLINK(x)        (((x) >> 8) & 0xFF)

#ifdef CONFIG_UI_LED_USE_PWM

#define UI_LED_ON_PERIOD_NORMAL        500
#define UI_LED_OFF_PERIOD_NORMAL    5000
#define UI_LED_ON_PERIOD_ERROR        500
#define UI_LED_OFF_PERIOD_ERROR        500

#define  UI_LED_BLINK_NORMAL    200

#define UI_LED_MAX            50
#define UI_BLUE_LED_MAX     100

#define UI_LED_COLOR_OFF        LED_COLOR(0, 0, 0)
#define UI_LED_COLOR_RED        LED_COLOR(UI_LED_MAX, 0, 0)
#define UI_LED_COLOR_GREEN        LED_COLOR(0, UI_LED_MAX, 0)
#define UI_LED_COLOR_BLUE        LED_COLOR(0, 0, UI_BLUE_LED_MAX)
#define UI_LED_COLOR_WHITE        LED_COLOR(UI_LED_MAX, UI_LED_MAX,      \
                          UI_BLUE_LED_MAX)
#define UI_LED_COLOR_YELLOW        LED_COLOR(UI_LED_MAX, UI_LED_MAX, 0)
#define UI_LED_COLOR_CYAN        LED_COLOR(0, UI_LED_MAX, UI_BLUE_LED_MAX)
#define UI_LED_COLOR_PURPLE        LED_COLOR(UI_LED_MAX, 0, UI_BLUE_LED_MAX)

#define UI_LTE_DISCONNECTED_COLOR    UI_LED_COLOR_OFF
/*#define UI_LTE_CONNECTING_COLOR        UI_LED_COLOR_WHITE*/
#define UI_LTE_CONNECTING_COLOR        UI_LED_COLOR_BLUE
#define UI_LTE_CONNECTED_COLOR        UI_LED_COLOR_CYAN
#define UI_CLOUD_CONNECTING_COLOR    UI_LED_COLOR_CYAN
#define UI_CLOUD_CONNECTED_COLOR    UI_LED_COLOR_BLUE
#define UI_CLOUD_PAIRING_COLOR        UI_LED_COLOR_YELLOW
#define UI_ACCEL_CALIBRATING_COLOR    UI_LED_COLOR_PURPLE
#define UI_LED_ERROR_CLOUD_COLOR    UI_LED_COLOR_RED
#define UI_LED_ERROR_BSD_REC_COLOR    UI_LED_COLOR_RED
#define UI_LED_ERROR_BSD_IRREC_COLOR    UI_LED_COLOR_RED
#define UI_LED_ERROR_LTE_LC_COLOR    UI_LED_COLOR_RED
#define UI_LED_ERROR_UNKNOWN_COLOR    UI_LED_COLOR_WHITE
#define UI_LED_GPS_SEARCHING_COLOR    UI_LED_COLOR_PURPLE
#define UI_LED_GPS_BLOCKED_COLOR    UI_LED_COLOR_BLUE
#define UI_LED_GPS_FIX_COLOR        UI_LED_COLOR_GREEN

#define  LTE_CONNECT_MASK        0x01
#define  BAT_CHARGING_MASK        0x02
#define     GPS_ACTIVE_MASK        0x04
#define  ALL_UI_MASK            (LTE_CONNECT_MASK|BAT_CHARGING_MASK|GPS_ACTIVE_MASK)

/*  LED always on */
#define   UI_WAIT_FOR_WALLET    (ALL_UI_MASK+1)  /*  BLUE */
#define   UI_WAIT_FOR_BUTTON_PRESSED   (UI_WAIT_FOR_WALLET+1)  /* Yellow */
#define   UI_WAIT_ACK                    (UI_WAIT_FOR_BUTTON_PRESSED+1) /*  white */
#define   UI_REG_SUCCESS        (UI_WAIT_ACK+1)  /*  grenn */
#define   UI_REG_FAIL   (UI_REG_SUCCESS+1)  /*   purle */

#else

#define UI_LED_ON_PERIOD_NORMAL        500
#define UI_LED_OFF_PERIOD_NORMAL    500

#endif /* CONFIG_UI_LED_USE_PWM */

/**@brief UI LED state pattern definitions. */
enum ui_led_pattern {
#ifdef CONFIG_UI_LED_USE_PWM
    UI_NGPS_NBAT_NLTE,
    UI_NGPS_NBAT_LTE,
    UI_NGPS_BAT_NLTE,
    UI_NGPS_BAT_LTE,
    UI_GPS_NBAT_NLTE,
    UI_GPS_NBAT_LTE,
    UI_GPS_BAT_NLTE,
    UI_GPS_BAT_LTE,
    UI_LTE_DISCONNECTED,
    UI_LTE_CONNECTING,
    UI_LTE_CONNECTED,
    UI_CLOUD_CONNECTING,
    UI_CLOUD_CONNECTED,
    UI_CLOUD_PAIRING,
    UI_ACCEL_CALIBRATING,
    UI_LED_ERROR_CLOUD,
    UI_LED_ERROR_BSD_REC,
    UI_LED_ERROR_BSD_IRREC,
    UI_LED_ERROR_LTE_LC,
    UI_LED_ERROR_UNKNOWN,
    UI_LED_GPS_SEARCHING,
    UI_LED_GPS_BLOCKED,
    UI_LED_GPS_FIX
#else /* LED patterns without using PWM. */
    UI_LTE_DISCONNECTED    = UI_LED_ON(0),
    UI_LTE_CONNECTING    = UI_LED_BLINK(DK_LED3_MSK),
    UI_LTE_CONNECTED    = UI_LED_ON(DK_LED3_MSK),
    UI_CLOUD_CONNECTING    = UI_LED_BLINK(DK_LED4_MSK),
    UI_CLOUD_CONNECTED    = UI_LED_ON(DK_LED4_MSK),
    UI_LED_GPS_SEARCHING    = UI_LED_ON(DK_LED4_MSK),
    UI_LED_GPS_FIX        = UI_LED_ON(DK_LED4_MSK),
    UI_LED_GPS_BLOCKED      = UI_LED_ON(DK_LED4_MSK),
    UI_CLOUD_PAIRING    = UI_LED_BLINK(DK_LED3_MSK | DK_LED4_MSK),
    UI_ACCEL_CALIBRATING    = UI_LED_ON(DK_LED1_MSK | DK_LED3_MSK),
    UI_LED_ERROR_CLOUD    = UI_LED_BLINK(DK_LED1_MSK | DK_LED4_MSK),
    UI_LED_ERROR_BSD_REC    = UI_LED_BLINK(DK_LED1_MSK | DK_LED3_MSK),
    UI_LED_ERROR_BSD_IRREC    = UI_LED_BLINK(DK_ALL_LEDS_MSK),
    UI_LED_ERROR_LTE_LC    = UI_LED_BLINK(DK_LED1_MSK | DK_LED2_MSK),
    UI_LED_ERROR_UNKNOWN    = UI_LED_ON(DK_ALL_LEDS_MSK)
#endif /* CONFIG_UI_LED_USE_PWM */
};



/**@brief UI event types. */
enum ui_evt_type {
    UI_EVT_BUTTON_ACTIVE,
    UI_EVT_BUTTON_INACTIVE
};

/**@brief UI event structure. */
struct ui_evt {
    enum ui_evt_type type;

    union {
        u32_t button;
    };
};

/**
 * @brief UI callback handler type definition.
 *
 * @param evt Pointer to event struct.
 */
typedef void (*ui_callback_t)(struct ui_evt evt);

/**
 * @brief Initializes the user interface module.
 *
 * @param cb UI callback handler. Can be NULL to disable callbacks.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_init(ui_callback_t cb);

/**
 * @brief Sets the LED pattern.
 *
 * @param pattern LED pattern.
 */
void ui_led_set_pattern(enum ui_led_pattern pattern);

/**
 * @brief Sets a LED's state. Only the one LED is affected, the rest of the
 *      LED pattern is preserved. Only has effect if the specified LED is not
 *      controlled by PWM.
 *
 * @param led LED number to be controlled.
 * @param value 0 turns the LED off, a non-zero value turns the LED on.
 */
void ui_led_set_state(u32_t led, u8_t value);

/**
 * @brief Gets the LED pattern.
 *
 * @return Current LED pattern.
 */
enum ui_led_pattern ui_led_get_pattern(void);

/**
 * @brief Sets the LED RGB color.
 *
 * @param red Red, in range 0 - 255.
 * @param green Green, in range 0 - 255.
 * @param blue Blue, in range 0 - 255.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_led_set_color(u8_t red, u8_t green, u8_t blue);

/**
 * @brief Get the state of a button.
 *
 * @param button Button number.
 *
 * @return 1 if button is active, 0 if it's inactive.
 */
bool ui_button_is_active(u32_t button);

/**
 * @brief Set the buzzer frequency.
 *
 * @param frequency Frequency. If set to 0, the buzzer is disabled.
 *            The frequency is limited to the range 100 - 10 000 Hz.
 * @param intensity Intensity of the buzzer output. If set to 0, the buzzer is
 *            disabled.
 *            The frequency is limited to the range 0 - 100 %.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_buzzer_set_frequency(u32_t frequency, u8_t intensity);

/**
 * @brief Write value to pin controlling NMOS transistor.
 *
 * @param nmos_idx    NMOS to control.
 * @param value        1 sets high signal on NMOS gate, 0 sets it low.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_nmos_write(size_t nmos_idx, u8_t value);

/**
 * @brief Control NMOS with PWM signal.
 *
 * @param period    PWM signal period in microseconds.
 * @param pulse    PWM signal Pulse in microseconds.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_nmos_pwm_set(size_t nmos_idx, u32_t period, u32_t pulse);

void ui_led_active(u8_t mask, u8_t flg);
void ui_led_deactive(u8_t mask, u8_t flg);
int onBeepMePressed(int ms);
bool isMask(u8_t mask);
void SetIndicator(int indct);
#ifdef __cplusplus
}
#endif

#endif /* UI_H__ */
