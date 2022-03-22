#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <power/reboot.h>
#include <logging/log_ctrl.h>
#include <logging/log.h>
#if defined(CONFIG_BSD_LIBRARY)
#include <modem/bsdlib.h>
#include <bsd.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#endif /* CONFIG_BSD_LIBRARY */
#include <net/cloud.h>
#include <net/socket.h>
#include <net/nrf_cloud.h>
#if defined(CONFIG_NRF_CLOUD_AGPS)
#include <net/nrf_cloud_agps.h>
#endif
#if defined(CONFIG_LWM2M_CARRIER)
#include <lwm2m_carrier.h>
#endif
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <dfu/mcuboot.h>
#endif
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <sys/mutex.h>
#include "dis_data.h"
#include "bme/bme680_helper.h"

LOG_MODULE_REGISTER(display, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#define I2C_ADDR 0x3c
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;


static void ssd1306_write_byte(uint8_t chData, uint8_t chCmd);
void ssd1306_display_on(void);
void ssd1306_display_off(void);
void ssd1306_refresh_gram(void);
void ssd1306_clear_screen(uint8_t chFill);  
void ssd1306_draw_point(uint8_t chXpos, uint8_t chYpos, uint8_t chPoint);
void ssd1306_fill_screen(uint8_t chXpos1, uint8_t chYpos1, uint8_t chXpos2, uint8_t chYpos2, uint8_t chDot);  
void ssd1306_display_char(uint8_t chXpos, uint8_t chYpos, uint8_t chChr, uint8_t chSize, uint8_t chMode);
void ssd1306_display_string(uint8_t chXpos, uint8_t chYpos, const uint8_t *pchString, uint8_t chSize, uint8_t chMode);
void ssd1306_display_logo(void);
void ssd1306_init(void);


#define SSD1306_CMD    0
#define SSD1306_DAT    1
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64


uint8_t s_chDispalyBuffer[128][8];
static struct device *__i2c_dev_CD1306;

struct sys_mutex iotex_i2c_access;

extern  struct sys_mutex iotex_hint_mutex;

static void ssd1306_write_byte(uint8_t chData, uint8_t chCmd)  {
    uint8_t chip = I2C_ADDR;
    unsigned int devaddr;
    if (chCmd) {
        devaddr= 0x40;
    } else {
        devaddr= 0x00;
    }
    i2c_reg_write_byte(__i2c_dev_CD1306, chip,devaddr, chData);
}

void ssd1306_display_on(void) {
    ssd1306_write_byte(0x8D, SSD1306_CMD);
    ssd1306_write_byte(0x14, SSD1306_CMD);
    ssd1306_write_byte(0xAF, SSD1306_CMD);
}

/**
  * @brief  OLED turns off
  *        
  * @param  None
  *        
  * @retval  None
**/
void ssd1306_display_off(void) {
    ssd1306_write_byte(0x8D, SSD1306_CMD);
    ssd1306_write_byte(0x10, SSD1306_CMD);
    ssd1306_write_byte(0xAE, SSD1306_CMD);
}

void ssd1306_refresh_gram(void) {
    uint8_t i, j;

    for (i = 0; i < 8; i ++) {
        ssd1306_write_byte(0xB0 + i, SSD1306_CMD);
        ssd1306_write_byte(0x00, SSD1306_CMD);
        ssd1306_write_byte(0x10, SSD1306_CMD);
        for (j = 0; j < 128; j ++) {
            ssd1306_write_byte(s_chDispalyBuffer[j][i], SSD1306_DAT);
        }
    }
}

void ssd1306_clear_screen(uint8_t chFill) {
    memset(s_chDispalyBuffer,chFill, sizeof(s_chDispalyBuffer));
    ssd1306_refresh_gram();
}

void ssd1306_clear_buffer(uint8_t chFill) {
    memset(s_chDispalyBuffer,chFill, sizeof(s_chDispalyBuffer));
}

/**
  * @brief  Draws a piont on the screen
  *        
  * @param  chXpos: Specifies the X position
  * @param  chYpos: Specifies the Y position
  * @param  chPoint: 0: the point turns off    1: the piont turns on 
  *        
  * @retval None
**/
void ssd1306_draw_point(uint8_t chXpos, uint8_t chYpos, uint8_t chPoint) {
    uint8_t chPos, chBx, chTemp = 0;
    
    if (chXpos > 127 || chYpos > 63) {
        return;
    }
    chPos = 7 - chYpos / 8;
    chBx = chYpos % 8;
    chTemp = 1 << (7 - chBx);
    
    if (chPoint) {
        s_chDispalyBuffer[chXpos][chPos] |= chTemp;
    } else {
        s_chDispalyBuffer[chXpos][chPos] &= ~chTemp;
    }
}

/**
  * @brief  Fills a rectangle
  *        
  * @param  chXpos1: Specifies the X position 1 (X top left position)
  * @param  chYpos1: Specifies the Y position 1 (Y top left position)
  * @param  chXpos2: Specifies the X position 2 (X bottom right position)
  * @param  chYpos3: Specifies the Y position 2 (Y bottom right position)
  *        
  * @retval 
**/
void ssd1306_fill_screen(uint8_t chXpos1, uint8_t chYpos1, uint8_t chXpos2, uint8_t chYpos2, uint8_t chDot) {
    uint8_t chXpos, chYpos;

    for (chXpos = chXpos1; chXpos <= chXpos2; chXpos ++) {
        for (chYpos = chYpos1; chYpos <= chYpos2; chYpos ++) {
            ssd1306_draw_point(chXpos, chYpos, chDot);
        }
    }
    
    ssd1306_refresh_gram();
}

/**
  * @brief Displays one character at the specified position    
  *        
  * @param  chXpos: Specifies the X position
  * @param  chYpos: Specifies the Y position
  * @param  chSize: 
  * @param  chMode
  * @retval 
**/
void ssd1306_display_char(uint8_t chXpos, uint8_t chYpos, uint8_t chChr, uint8_t chSize, uint8_t chMode) {
    uint8_t i, j;
    uint8_t chTemp, chYpos0 = chYpos;

    chChr = chChr - ' ';
    for (i = 0; i < chSize; i ++) {
        if (chMode) {
            chTemp = c_chFont1608[chChr][i];
        } else {
            chTemp = ~c_chFont1608[chChr][i];
        }
        
        for (j = 0; j < 8; j ++) {
            if (chTemp & 0x80) {
                ssd1306_draw_point(chXpos, chYpos, 1);
            } else {
                ssd1306_draw_point(chXpos, chYpos, 0);
            }
            chTemp <<= 1;
            chYpos ++;

            if ((chYpos - chYpos0) == chSize) {
                chYpos = chYpos0;
                chXpos ++;
                break;
            }
        }
    }
}

uint8_t *getEndpos(const uint8_t *str) {
    uint32_t i;
    for(i = 0; i<15; i++) {
        if(*str != '\0') {
            str++;
        } else {
            break;
        }
    }

    if (((*str >= 'a') && (*str <= 'z')) || ((*str >= 'A') && (*str <= 'Z'))) {
        if (((*(str+1) >= 'a') && (*(str+1) <= 'z')) || ((*(str+1) >= 'A') && (*(str+1) <= 'Z'))) {
            while((*str != ' ') && (*str != ',')) {
                str--;
            }
        }
    }

    return (str + 1);
}

/**
  * @brief  Displays a string on the screen
  *        
  * @param  chXpos: Specifies the X position
  * @param  chYpos: Specifies the Y position
  * @param  pchString: Pointer to a string to display on the screen 
  *        
  * @retval  None
**/
void ssd1306_display_string(uint8_t chXpos, uint8_t chYpos, const uint8_t *pchString, uint8_t chSize, uint8_t chMode) {
    uint8_t *endstr;
    endstr = getEndpos(pchString);
    while (*pchString != '\0') {
        if ((chXpos > (SSD1306_WIDTH - chSize / 2)) || (pchString == endstr)) {
            endstr = getEndpos(pchString);
            chXpos = 0;
            chYpos += chSize;
            if (chYpos > (SSD1306_HEIGHT - chSize)) {
                chYpos = chXpos = 0;
                ssd1306_clear_screen(0x00);
            }
        }
        
        ssd1306_display_char(chXpos, chYpos, *pchString, chSize, chMode);
        chXpos += chSize / 2;
        pchString ++;
    }
}

void ssd1306_init(void) {
    if (!(__i2c_dev_CD1306 = device_get_binding(I2C_DEV_BME680))) {
        LOG_ERR("I2C: Device driver[%s] not found.\n", I2C_DEV_BME680);
        return ;
    }
    sys_mutex_init(&iotex_i2c_access);
    /* ssd1306_write_byte(0xAE, SSD1306_CMD);--turn off oled panel */
    ssd1306_write_byte(0x00, SSD1306_CMD);/* ---set low column address */
    ssd1306_write_byte(0x10, SSD1306_CMD);/* ---set high column address */
    ssd1306_write_byte(0x40, SSD1306_CMD);/* --set start line address  Set Mapping RAM Display Start Line (0x00~0x3F) */
    ssd1306_write_byte(0x81, SSD1306_CMD);/* --set contrast control register */
    ssd1306_write_byte(0xCF, SSD1306_CMD);/*  Set SEG Output Current Brightness */
    ssd1306_write_byte(0xA1, SSD1306_CMD);/* --Set SEG/Column Mapping     */
    ssd1306_write_byte(0xC0, SSD1306_CMD);/* Set COM/Row Scan Direction   */
    ssd1306_write_byte(0xA6, SSD1306_CMD);/* --set normal display */
    ssd1306_write_byte(0xA8, SSD1306_CMD);/* --set multiplex ratio(1 to 64) */
    ssd1306_write_byte(0x3f, SSD1306_CMD);/* --1/64 duty */
    ssd1306_write_byte(0xD3, SSD1306_CMD);/* -set display offset    Shift Mapping RAM Counter (0x00~0x3F) */
    ssd1306_write_byte(0x00, SSD1306_CMD);/* - offset 20 */
    ssd1306_write_byte(0xd5, SSD1306_CMD);/* --set display clock divide ratio/oscillator frequency */
    ssd1306_write_byte(0x80, SSD1306_CMD);/* --set divide ratio, Set Clock as 100 Frames/Sec */
    ssd1306_write_byte(0xD9, SSD1306_CMD);/* --set pre-charge period */
    ssd1306_write_byte(0xF1, SSD1306_CMD);/* Set Pre-Charge as 15 Clocks & Discharge as 1 Clock */
    ssd1306_write_byte(0xDA, SSD1306_CMD);/* --set com pins hardware configuration */
    ssd1306_write_byte(0x12, SSD1306_CMD);
    ssd1306_write_byte(0xDB, SSD1306_CMD);/* --set vcomh */
    ssd1306_write_byte(0x40, SSD1306_CMD);/* Set VCOM Deselect Level */
    ssd1306_write_byte(0x20, SSD1306_CMD);/* -Set Page Addressing Mode (0x00/0x01/0x02) */
    ssd1306_write_byte(0x02, SSD1306_CMD);/*  */
    ssd1306_write_byte(0x8D, SSD1306_CMD);/* --set Charge Pump enable/disable */
    ssd1306_write_byte(0x14, SSD1306_CMD);/* --set(0x10) disable */
    ssd1306_write_byte(0xA4, SSD1306_CMD);/*  Disable Entire Display On (0xa4/0xa5) */
    ssd1306_write_byte(0xA6, SSD1306_CMD);/*  Disable Inverse Display On (0xa6/a7)  */
    hintInit();
}

void ctrlOLED(bool on_off) {
    ssd1306_write_byte(on_off ? 0xAF : 0xAE, SSD1306_CMD);
}

void i2cLock(void) {
    sys_mutex_lock(&iotex_i2c_access, K_FOREVER);
}

void i2cUnlock(void) {
    sys_mutex_unlock(&iotex_i2c_access);
}

void ssd1306_refresh_lines(uint8_t start_line, uint8_t stop_line) {
    uint8_t i, j;

    i2cLock();
    for (i = start_line; i <= stop_line ; i ++) {
        ssd1306_write_byte(0xB0 + i, SSD1306_CMD);
        ssd1306_write_byte(0x00, SSD1306_CMD);
        ssd1306_write_byte(0x10, SSD1306_CMD);
        for (j = 0; j < SSD1306_WIDTH; j ++) {
            ssd1306_write_byte(s_chDispalyBuffer[j][i], SSD1306_DAT);
        }
    }
    i2cUnlock();
}

void clearDisBuf(uint8_t start_line, uint8_t stop_line) {
    uint8_t i, j;
    for (i = start_line; i <= stop_line; i ++) {
        for (j = 0; j < 128; j ++) {
            s_chDispalyBuffer[j][i]= 0;
        }
    }
}

void ssd1306_display_logo(void) {
    uint8_t i, j;
    sys_mutex_lock(&iotex_hint_mutex, K_FOREVER);
    for (i = 0; i < 6; i ++) {
        for (j = 0; j < 128; j ++) {
            s_chDispalyBuffer[j][i]= s_Iotex_logo[j + i * 128];
        }
    }
    ssd1306_refresh_lines(0, 5);
    sys_mutex_unlock(&iotex_hint_mutex);
}
