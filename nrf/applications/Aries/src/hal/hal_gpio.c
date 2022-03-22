#include <zephyr.h>
#include <drivers/gpio.h>
#include <drivers/uart.h>
#include <logging/log.h>
#include "hal_gpio.h"
#include "ui.h"
#include "modem/modem_helper.h"
#include "nvs/local_storage.h"
#include "display.h"

LOG_MODULE_REGISTER(hal_gpio, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define GPIO_DIR_OUT  GPIO_OUTPUT
#define GPIO_DIR_IN   GPIO_INPUT
#define GPIO_INT  GPIO_INT_ENABLE
#define GPIO_INT_DOUBLE_EDGE  GPIO_INT_EDGE_BOTH
#define gpio_pin_write  gpio_pin_set


#define UART_COMTOOL  "UART_1"   
struct device *__gpio0_dev;
static u32_t g_key_press_start_time;
static struct gpio_callback chrq_gpio_cb, pwr_key_gpio_cb;
static struct device *guart_dev_comtool;
static atomic_val_t uartReceiveLen = ATOMIC_INIT(0);
struct k_delayed_work  uartIRQMonit;
unsigned char *uartReceiveBuff, *uartTransBuff;

static void cmdACKReadEcc(enum COM_COMMAND cmd, unsigned char *data,unsigned int len);
static void buildPackage(enum COM_COMMAND cmd, unsigned char *data, unsigned short data_len);
static void packageProcess(void);

const  void (*cmdACK[])(enum COM_COMMAND, unsigned char *, unsigned int) = {
    NULL,
    cmdACKReadEcc,
};

extern atomic_val_t modemWriteProtect;

/* extern struct k_delayed_work  event_work; */

void checkCHRQ(void) {
    u32_t chrq;
    chrq = gpio_pin_get(__gpio0_dev, IO_NCHRQ);
    if (!chrq) {
        /*  charging */
        gpio_pin_write(__gpio0_dev, LED_RED, LED_ON);
        sta_SetMeta(PEBBLE_POWER, STA_LINKER_ON);
    } else {
        /*  not charging */
        gpio_pin_write(__gpio0_dev, LED_RED, LED_OFF);
        sta_SetMeta(PEBBLE_POWER, STA_LINKER_OFF);
    }
}

void CtrlBlueLED(bool on_off) {
    if(on_off)
        gpio_pin_write(__gpio0_dev, LED_BLUE, LED_ON);
    else
        gpio_pin_write(__gpio0_dev, LED_BLUE, LED_OFF);
}

static void chrq_input_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {
    checkCHRQ();
}

static void pwr_key_callback(struct device *port, struct gpio_callback *cb, u32_t pins) {
    u32_t pwr_key, end_time;
    int32_t key_press_duration, ret;

    pwr_key = gpio_pin_get(port, POWER_KEY);

    if (IS_KEY_PRESSED(pwr_key)) {
        g_key_press_start_time = k_uptime_get_32();
        LOG_DBG("Power key pressed:[%u]\n", g_key_press_start_time);
    } else {
        end_time = k_uptime_get_32();
        LOG_DBG("Power key released:[%u]\n", end_time);

        if (end_time > g_key_press_start_time) {
            key_press_duration = end_time - g_key_press_start_time;
        } else {
            key_press_duration = end_time + (int32_t)g_key_press_start_time;
        }

        LOG_DBG("Power key press duration: %d ms\n", key_press_duration);

        if (key_press_duration > KEY_POWER_OFF_TIME) {
            gpio_pin_write(port, IO_POWER_ON, POWER_OFF);
        }
    }
}

void iotex_hal_gpio_init(void) {
    __gpio0_dev = device_get_binding("GPIO_0");

    /* Set LED pin as output */
    gpio_pin_configure(__gpio0_dev, IO_POWER_ON, GPIO_DIR_OUT);    /* p0.31 == POWER_ON */
    gpio_pin_configure(__gpio0_dev, LED_GREEN, GPIO_DIR_OUT);     /* p0.00 == LED_GREEN */
    gpio_pin_configure(__gpio0_dev, LED_BLUE, GPIO_DIR_OUT);    /* p0.01 == LED_BLUE */
    gpio_pin_configure(__gpio0_dev, LED_RED, GPIO_DIR_OUT);     /* p0.02 == LED_RED */
    gpio_pin_write(__gpio0_dev, IO_POWER_ON, POWER_ON);    /* p0.31 == POWER_ON */
    gpio_pin_write(__gpio0_dev, LED_GREEN, LED_OFF);    /* p0.00 == LED_GREEN ON */
    gpio_pin_write(__gpio0_dev, LED_BLUE, LED_OFF);    /* p0.00 == LED_BLUE OFF */
    gpio_pin_write(__gpio0_dev, LED_RED, LED_OFF);    /* p0.00 == LED_RED */
    /* USB battery charge pin */
    gpio_pin_configure(__gpio0_dev, IO_NCHRQ,
                    (GPIO_DIR_IN | GPIO_INT |
                    GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
                    GPIO_INT_DEBOUNCE));
    gpio_init_callback(&chrq_gpio_cb, chrq_input_callback, BIT(IO_NCHRQ));
    gpio_add_callback(__gpio0_dev, &chrq_gpio_cb);
    gpio_pin_interrupt_configure(__gpio0_dev, IO_NCHRQ,GPIO_INT_EDGE_BOTH);
    /* Sync charge state */
    chrq_input_callback(__gpio0_dev, &chrq_input_callback, IO_NCHRQ);
    checkCHRQ();
}

int iotex_hal_gpio_set(uint32_t pin, uint32_t value) {
    return gpio_pin_write(__gpio0_dev, pin, value);
}

void gpio_poweroffNotice(void)
{
    /* wait for modem flash writing over */
    if(atomic_get(&modemWriteProtect))
        return;
    /* qhm add 0830*/
    gpio_pin_write(__gpio0_dev, LED_GREEN, LED_ON);
    k_sleep(K_MSEC(300));
    gpio_pin_write(__gpio0_dev, LED_GREEN, LED_OFF);
    k_sleep(K_MSEC(300));
    gpio_pin_write(__gpio0_dev, LED_GREEN, LED_ON);
    k_sleep(K_MSEC(300));
    gpio_pin_write(__gpio0_dev, LED_GREEN, LED_OFF);
    k_sleep(K_MSEC(300));
}

void gpio_poweroff(void)
{
    /* wait for modem flash writing over */
    if(atomic_get(&modemWriteProtect))
        return;
    /* qhm add 0830*/
    gpio_pin_write(__gpio0_dev, LED_RED, LED_ON);
    k_sleep(K_MSEC(300));
    gpio_pin_write(__gpio0_dev, LED_RED, LED_OFF);
    k_sleep(K_MSEC(300));
    gpio_pin_write(__gpio0_dev, LED_RED, LED_ON);
    k_sleep(K_MSEC(300));
    gpio_pin_write(__gpio0_dev, LED_RED, LED_OFF);
    k_sleep(K_MSEC(300));
    /*end of qhm add 0830*/
    gpio_pin_write(__gpio0_dev, IO_POWER_ON, POWER_OFF);
}

void PowerOffIndicator(void)
{
    for (int i = 0; i < 3; i++) {
        gpio_pin_write(__gpio0_dev, LED_RED, LED_ON);
        k_sleep(K_MSEC(1000));
        gpio_pin_write(__gpio0_dev, LED_RED, LED_OFF);
        k_sleep(K_MSEC(1000));
    }
    gpio_poweroff();
    k_sleep(K_MSEC(5000));
}

static void uart_comtool_cb(struct device *dev) {
    unsigned int fifo_count; 
    uart_irq_update(dev);
    if (!uart_irq_rx_ready(dev)) {
        return;
    }
    do {
        fifo_count=uart_fifo_read(dev, &uartReceiveBuff[atomic_get(&uartReceiveLen)], 200); 
        atomic_add(&uartReceiveLen, fifo_count);
        atomic_and(&uartReceiveLen, 0x0FFF); 
    }while(fifo_count);
    k_delayed_work_submit(&uartIRQMonit,K_MSEC(300));
}

void uartIRQMonit_callback(struct k_work *work) {

    if(atomic_get(&uartReceiveLen)) {
        uart_irq_rx_disable(guart_dev_comtool);
        packageProcess();
        atomic_set(&uartReceiveLen, 0);
        uart_irq_rx_enable(guart_dev_comtool);
    }
    else
        k_delayed_work_submit(&uartIRQMonit,K_MSEC(300));
}

void ComToolInit(void) { 
    struct device *dev;
    uartReceiveBuff = malloc(4096+256);
    uartTransBuff = malloc(1024);
    if(uartTransBuff == NULL || uartReceiveBuff == NULL) {
        LOG_INF("mem not enought \n");
        if(uartTransBuff != NULL)
            free(uartTransBuff);
        if(uartReceiveBuff != NULL)
            free(uartReceiveBuff);
        return;
    }
    atomic_set(&uartReceiveLen, 0);
    guart_dev_comtool=device_get_binding(UART_COMTOOL); 
    uart_irq_callback_set(guart_dev_comtool, uart_comtool_cb);
    k_delayed_work_init(&uartIRQMonit, uartIRQMonit_callback);
    uart_irq_rx_enable(guart_dev_comtool);
    uart_irq_tx_disable(guart_dev_comtool);
}
void stopTestCom(void) {
    if(uartTransBuff != NULL)
        free(uartTransBuff);
    if(uartReceiveBuff != NULL)
        free(uartReceiveBuff);    
    uart_irq_rx_disable(guart_dev_comtool);
    k_delayed_work_cancel(&uartIRQMonit);
}

static bool checkAndACK(unsigned char *buf, unsigned  int len) {
    unsigned char *pbuf = buf;
    unsigned  int  i = len;
    unsigned char cmd;

    while(i--) {
        if(*pbuf++ == CMD_HEAD)
            break;
    }
    
    if( i < 5 )
        return  false;
    i -= 2; 
    
    if(((*pbuf) < COM_CMD_INIT) || ((*pbuf) >= COM_CMD_LAST))
        return false; 
    if(CRC16(pbuf, i) != (unsigned short)(((*(pbuf+i))<<8)|(*(pbuf+i+1)))){
        return  false;
    }
    // it's legal package
    cmd = *pbuf;
    cmdACK[cmd-COM_CMD_INIT](cmd, pbuf+3, i-3);
}

static bool isLegacySymble(uint8_t c) {
    if(((c>='0') && (c<='9')) || ((c>='A') && (c <= 'Z')))
        return true;
    else    
        return false;
}
static bool isActiveSn(uint8_t *sn) {
    for (int i = 0; i < 10; i++) {
        if (!isLegacySymble(sn[i]))
            return false;
    }
    return true;
}

static bool snExist(uint8_t *sn)
{
    uint8_t buf[300];
    uint8_t *pbuf;
    uint8_t devSN[20];

    memset(buf, 0, sizeof(buf));
    pbuf = ReadDataFromModem(PEBBLE_DEVICE_SN, buf, sizeof(buf));
    if (pbuf != NULL) {
        memcpy(devSN, pbuf, 10);
        devSN[10] = 0;
        LOG_INF("SN:%s\n", devSN);
        if (isActiveSn(devSN)) {
            memcpy(sn, devSN,10);
            return true;
        }
    }
    return false;
}
static void cmdACKReadEcc(enum COM_COMMAND cmd, unsigned char *data,unsigned int len) {
    uint8_t sn[11],precord[2];
    unsigned char *pub;
    uint8_t *package;
    unsigned char ack_cmd[]={0};
    int32_t ret;

    memset(sn,0, sizeof(sn));
    pub = readECCPubKey();
    if(pub == NULL)
        pub = "";
    package = malloc(2048);
    if(package != NULL) {
        precord[0] = '0';
        precord[1] = 0;
        if(snExist(sn))
            snprintf(package, 2048, "decc device-id:%s sn:%s ECC-Pub-Key_HexString(mpi_X,mpi_Y):%s %s ",iotex_mqtt_get_client_id(),sn,pub,precord);
        else
            snprintf(package, 2048, "decc device-id:%s sn:%s ECC-Pub-Key_HexString(mpi_X,mpi_Y):%s %s ",iotex_mqtt_get_client_id(),"",pub,precord);

        buildPackage(cmd, package, strlen(package));
    }
    else{
        ack_cmd[0] = 1;
        buildPackage(cmd, ack_cmd, sizeof(ack_cmd));
    }
    if(package)
        free(package);
}  
static void comtoolOut(uint8_t *buf, uint32_t len) {
    uint32_t i;
    for(i = 0; i < len; i++)
    {
        uart_poll_out(guart_dev_comtool,buf[i]); 
    }
    
}
static void packageProcess(void) {
    checkAndACK(uartReceiveBuff, atomic_get(&uartReceiveLen));
}
static void buildPackage(enum COM_COMMAND cmd, unsigned char *data, unsigned short data_len) {
    unsigned short crc;

    uartTransBuff[0] = CMD_HEAD;
    uartTransBuff[1] = cmd;
    uartTransBuff[2] = (unsigned char)(data_len >> 8);
    uartTransBuff[3] = (unsigned char)(data_len);
    memcpy(uartTransBuff+4, data, data_len);
    crc = CRC16(uartTransBuff+1, data_len+3);
    uartTransBuff[data_len + 4] = (unsigned char)(crc>>8);
    uartTransBuff[data_len+5] = (unsigned char)crc;
    comtoolOut(uartTransBuff, data_len+6);
}


void setI2Cspeed(unsigned int level) {
    volatile int *i2cFrq = 0x4000A524;
    unsigned int speed[] = {100,250,400};
    if(level >= sizeof(speed)/sizeof(int)) {
        LOG_ERR("Bad i2c speed level :%d\n", level);
        return;
    }

    LOG_INF("Read out i2c-2 speed configure : \n");
    switch (*i2cFrq) {
        case 0x01980000:
            LOG_INF("i2c-2 speed: 100kbps \n");
            break;
        case 0x04000000:
            LOG_INF("i2c-2 speed: 250kbps \n");
            break;
        case 0x06400000:
            LOG_INF("i2c-2 speed: 400kbps \n");
            break;
        default:
            LOG_INF("i2c-2 speed not supported \n");
            break;
    }

    LOG_INF("i2c-2 speed set to %dkbps \n",speed[level]);
    switch (level) {
        case 0:
            *i2cFrq = 0x01980000;
            break;
        case 1:
            *i2cFrq = 0x04000000;
            break;
        case 2:
            *i2cFrq = 0x06400000;
            break;
        default:
            LOG_INF("i2c-2 speed not supported \n");
            break;
    }

    LOG_INF("Now i2c-2 speed is :");
    switch (*i2cFrq) {
        case 0x01980000:
            LOG_INF("100kbps \n");
            break;
        case 0x04000000:
            LOG_INF("250kbps \n");
            break;
        case 0x06400000:
            LOG_INF("400kbps \n");
            break;
        default:
            LOG_ERR("speed not supported \n");
            break;
    }
}
