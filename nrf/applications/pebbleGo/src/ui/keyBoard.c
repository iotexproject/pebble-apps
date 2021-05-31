#include <zephyr.h>
#include <drivers/gpio.h>
#include "hal/hal_gpio.h"
#include "keyBoard.h"
#include  "mqtt/devReg.h"
#include "display.h"

#define GPIO_DIR_OUT  GPIO_OUTPUT
#define GPIO_DIR_IN   GPIO_INPUT
#define GPIO_INT  GPIO_INT_ENABLE
#define GPIO_INT_DOUBLE_EDGE  GPIO_INT_EDGE_BOTH
#define gpio_pin_write  gpio_pin_set


static struct gpio_callback up_key_gpio_cb, down_key_gpio_cb, pwr_key_gpio_cb;
static uint8_t pressedKey;
static struct k_delayed_work power_off_button_work;

extern struct device *__gpio0_dev;

void ClearKey(void)
{
    pressedKey = KB_NO_KEY;
}
uint8_t getKey(void)
{
    return pressedKey;
}

static void up_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {    
    u32_t key;    
    key = gpio_pin_get(port, IO_UP_KEY);
    if(key){
        pressedKey |= KB_UP_KEY;
        key = gpio_pin_get(port, POWER_KEY);
        if(key){
            k_delayed_work_cancel(&power_off_button_work);
        }        
        printk("up_key_release\n");      
    }
    else
    {
        printk("up_key_press\n"); 
    }
}

static void down_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {    
    u32_t key;
    key = gpio_pin_get(port, IO_DOWN_KEY);
    if(key){
        pressedKey |= KB_DOWN_KEY;
        printk("down_key_release\n"); 
    }
    else
    {
        printk("down_key_press\n"); 
    }
}

static void pwr_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {
    u32_t pwr_key;
    pwr_key = gpio_pin_get(port, POWER_KEY);

    if (!pwr_key) { 
        pressedKey |= KB_POWER_KEY;
        k_delayed_work_submit(&power_off_button_work,K_SECONDS(5));       
        printk("Power key pressed\n");
        if(IsDevReg())
        {
            if(devRegGet() == DEV_REG_WAIT_FOR_BUTTON_DOWN)
            {
                devRegSet(DEV_REG_SIGN_SEND);
                //SetIndicator(UI_WAIT_ACK);               
            }            
        }
        else
        {
            if(devRegGet() == DEV_UPGRADE_CONFIRM)
            {               
                devRegSet(DEV_UPGRADE_STARED);
            }
        }
    }
    else {
        k_delayed_work_cancel(&power_off_button_work);
        printk("Power key released\n");
    }
}

static void power_off_handler(struct k_work *work)
{        
    gpio_poweroff();
}

void iotex_key_init(void) { 
    /* up_key pin */
    gpio_pin_configure(__gpio0_dev, IO_UP_KEY,
                    (GPIO_DIR_IN | GPIO_INT | GPIO_PULL_UP |
                        GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                        GPIO_INT_DEBOUNCE));

    gpio_init_callback(&up_key_gpio_cb, up_key_callback, BIT(IO_UP_KEY));
    gpio_add_callback(__gpio0_dev, &up_key_gpio_cb);
    //gpio_pin_enable_callback(__gpio0_dev, IO_UP_KEY);
    gpio_pin_interrupt_configure(__gpio0_dev, IO_UP_KEY,GPIO_INT_EDGE_BOTH);

 /* down_key pin */
    gpio_pin_configure(__gpio0_dev, IO_DOWN_KEY,
                    (GPIO_DIR_IN | GPIO_INT | GPIO_PULL_UP |
                        GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                        GPIO_INT_DEBOUNCE));

    gpio_init_callback(&down_key_gpio_cb, down_key_callback, BIT(IO_DOWN_KEY));
    gpio_add_callback(__gpio0_dev, &down_key_gpio_cb);
    //gpio_pin_enable_callback(__gpio0_dev, IO_DOWN_KEY);
    gpio_pin_interrupt_configure(__gpio0_dev, IO_DOWN_KEY,GPIO_INT_EDGE_BOTH);

    /* Power key pin configure */
    k_delayed_work_init(&power_off_button_work, power_off_handler);
    gpio_pin_configure(__gpio0_dev, POWER_KEY,
                        (GPIO_DIR_IN | GPIO_INT |
                        GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                        GPIO_INT_DEBOUNCE));

    gpio_init_callback(&pwr_key_gpio_cb, pwr_key_callback, BIT(POWER_KEY));
    gpio_add_callback(__gpio0_dev, &pwr_key_gpio_cb);
    //gpio_pin_enable_callback(__gpio0_dev, POWER_KEY);
    gpio_pin_interrupt_configure(__gpio0_dev, POWER_KEY,GPIO_INT_EDGE_BOTH);
}