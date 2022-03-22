#ifndef _IOTEX_HAL_GPIO_H_
#define _IOTEX_HAL_GPIO_H_

#if (CONFIG_IOTEX_BOARD_VERSION == 3)
#define LED_GREEN     18         /* p0.00 == LED_GREEN  0=on 1=off */
#define LED_BLUE      17    /* p0.01 == LED_BLUE   0=on 1=off */
#define LED_RED       16    /* p0.02 == LED_RED    0=on 1=off */

#define IO_NCHRQ     5    /* p0.26 == CHRQ      0 = charg, 1 = off */
#define IO_POWER_ON  6   /* p0.31 == POWER_ON  1 = on, 0 = off */

#define POWER_KEY    15         /* p0.30 == POWER_KEY  0=down 1=up */
#elif (CONFIG_IOTEX_BOARD_VERSION == 2)
#define LED_GREEN     26         /* p0.00 == LED_GREEN  0=on 1=off */
#define LED_BLUE      27    /* p0.01 == LED_BLUE   0=on 1=off */
#define LED_RED       30    /* p0.02 == LED_RED    0=on 1=off */

#define IO_NCHRQ     12    /* p0.26 == CHRQ      0 = charg, 1 = off */
#define IO_POWER_ON  15   /* p0.31 == POWER_ON  1 = on, 0 = off */

#define POWER_KEY    14         /* p0.30 == POWER_KEY  0=down 1=up */
#endif

#define KEY_POWER_OFF_TIME 3000  /*   press power_key 3S to power off system */

#define LED_ON        0
#define LED_OFF       1

#define POWER_ON      1
#define POWER_OFF     0

#define KEY_PRESSED   0
#define KEY_RELEASED  1

#define IS_PWR_CHARGE(x)  ((x) == 0)
#define IS_KEY_PRESSED(x) ((x) == KEY_PRESSED)


void iotex_hal_gpio_init(void);
int iotex_hal_gpio_set(uint32_t pin, uint32_t value);
void gpio_poweroff(void);
void PowerOffIndicator(void);
void setI2Cspeed(unsigned int level);

// cmd head 
#define  CMD_HEAD    0xAA

// cmd  define
enum COM_COMMAND
{
    COM_CMD_INIT=0x20,
    COM_CMD_READ_ECC,
    COM_CMD_LAST
};

#endif /* _IOTEX_HAL_GPIO_H_ */