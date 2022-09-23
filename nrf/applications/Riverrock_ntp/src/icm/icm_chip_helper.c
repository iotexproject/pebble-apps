#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <drivers/i2c.h>
#include "icm42605/Icm426xxDefs.h"
#include "icm42605/Icm426xxTransport.h"
#include "icm42605/Icm426xxDriver_HL.h"
#include "icm42607/inv_imu_defs.h"
#include "icm42607/inv_imu_driver.h"
#include "icm42607/inv_imu_transport.h"
#include "icm42607/inv_imu_apex.h"
#include "icm42607/inv_imu_pedometer.h"
#include "icm_chip_helper.h"
#include "modem/modem_helper.h"
#include "nvs/local_storage.h"
#include "icm42605/icm426xxActionDetect.h"
#include "icm42605/icm426xx_pedometer.h"



static struct device *__i2c_dev_icm_chip;
static struct inv_icm426xx __icm_driver;
static struct inv_imu_device icm_driver;

uint8_t icm_chip_detect;    // 0: unknow device. 1:  icm-42605 . 2: icm-42607p

static int32_t icm_mounting_matrix[9] = {  0,     -(1<<30), 0,(1<<30), 0,       0, 0,      0,      (1<<30) };

void inv_icm426xx_sleep_us(uint32_t us) {
    k_busy_wait(us);
}

uint64_t inv_icm426xx_get_time_us(void) {
    return (SYS_CLOCK_HW_CYCLES_TO_NS64(k_cycle_get_32()) / 1000);
}
/* ICM-42605  i2C interface*/
static int inv_icm426xx_io_hal_read_reg(struct inv_icm426xx_serif *serif, uint8_t reg, uint8_t *rbuffer, uint32_t rlen) {
    return i2c_burst_read(__i2c_dev_icm_chip, I2C_ADDR_ICM42605, reg, rbuffer, rlen);
}

static int inv_icm426xx_io_hal_write_reg(struct inv_icm426xx_serif *serif, uint8_t reg, const uint8_t *wbuffer, uint32_t wlen) {
    uint8_t rslt = 0;

    for (uint32_t i = 0; i < wlen; i++) {
        if ((rslt =  i2c_reg_write_byte(__i2c_dev_icm_chip, I2C_ADDR_ICM42605, reg + i, wbuffer[i]))) {
            return rslt;
        }
    }
    return rslt;
}
/* ICM-42607P  i2C interface*/
static int inv_io_hal_read_reg(struct inv_imu_serif *serif, uint8_t reg, uint8_t *rbuffer, uint32_t rlen) {
    return i2c_burst_read(__i2c_dev_icm_chip, I2C_ADDR_ICM42607, reg, rbuffer, rlen);
}

static int inv_io_hal_write_reg(struct inv_imu_serif *serif, uint8_t reg, const uint8_t *wbuffer, uint32_t wlen) {
    uint8_t rslt = 0;

    for (uint32_t i = 0; i < wlen; i++) {
        if ((rslt =  i2c_reg_write_byte(__i2c_dev_icm_chip, I2C_ADDR_ICM42607, reg + i, wbuffer[i]))) {
            return rslt;
        }
    }
    return rslt;
}

static int iotex_icm42605_configure(uint8_t is_low_noise_mode,
                                    ICM426XX_ACCEL_CONFIG0_FS_SEL_t acc_fsr_g,
                                    ICM426XX_GYRO_CONFIG0_FS_SEL_t gyr_fsr_dps,
                                    ICM426XX_ACCEL_CONFIG0_ODR_t acc_freq,
                                    ICM426XX_GYRO_CONFIG0_ODR_t gyr_freq,
                                    uint8_t is_rtc_mode)
{
    int rc = 0;
    uint32_t startup_delay;

    rc |= inv_icm426xx_enable_clkin_rtc(&__icm_driver, is_rtc_mode);

    rc |= inv_icm426xx_set_accel_fsr(&__icm_driver, acc_fsr_g);
    rc |= inv_icm426xx_set_gyro_fsr(&__icm_driver, gyr_fsr_dps);

    rc |= inv_icm426xx_set_accel_frequency(&__icm_driver, acc_freq);
    rc |= inv_icm426xx_set_gyro_frequency(&__icm_driver, gyr_freq);

    if (is_low_noise_mode)
        rc |= inv_icm426xx_enable_accel_low_noise_mode(&__icm_driver);
    else
        rc |= inv_icm426xx_enable_accel_low_power_mode(&__icm_driver);

    rc |= inv_icm426xx_enable_gyro_low_noise_mode(&__icm_driver);

    /* Wait Max of ICM426XX_GYR_STARTUP_TIME_US and ICM426XX_ACC_STARTUP_TIME_US*/
    startup_delay = (ICM426XX_GYR_STARTUP_TIME_US > ICM426XX_ACC_STARTUP_TIME_US) ? ICM426XX_GYR_STARTUP_TIME_US : ICM426XX_ACC_STARTUP_TIME_US;
    inv_icm426xx_sleep_us(startup_delay);

    return rc;
}

int  rawdataConf(void)
{
    return iotex_icm42605_configure((uint8_t)IS_LOW_NOISE_MODE,
                                    ICM426XX_ACCEL_CONFIG0_FS_SEL_4g,
                                    ICM426XX_GYRO_CONFIG0_FS_SEL_2000dps,
                                    ICM426XX_ACCEL_CONFIG0_ODR_1_KHZ,
                                    ICM426XX_GYRO_CONFIG0_ODR_1_KHZ,
                                    (uint8_t)TMST_CLKIN_32K);  
}

int  rawdataDetect(void)
{
    int status = 0;
    uint8_t int_status;    
    status |= inv_icm426xx_read_reg(&__icm_driver, MPUREG_INT_STATUS, 1, &int_status);
    if (status) {
        printk("error rawdata: %d\n",int_status);        
        return 0;
    }
    if (int_status & BIT_INT_STATUS_DRDY) {
        printk("rawdata detected \n");
    }
    return 1;
}

static int setup_icm42607(struct inv_imu_serif *icm_serif)
{
	int rc = 0;
	uint8_t who_am_i;
    

	/* Initialize serial interface between MCU and IMU */
	icm_serif->context   = 0;        /* no need */
	icm_serif->read_reg  = inv_io_hal_read_reg;
	icm_serif->write_reg = inv_io_hal_write_reg;
	icm_serif->max_read  = 1024*32;  /* maximum number of bytes allowed per serial read */
	icm_serif->max_write = 1024*32;  /* maximum number of bytes allowed per serial write */
	icm_serif->serif_type = ICM42607_SERIF_TYPE;

	/* Init device */
	rc = inv_imu_init(&icm_driver, icm_serif, NULL);
	if (rc != INV_ERROR_SUCCESS) {
		printk("Failed to initialize IMU!\n");
		return rc;
	}
	
	/* Check WHOAMI */
	rc = inv_imu_get_who_am_i(&icm_driver, &who_am_i);
	if (rc != INV_ERROR_SUCCESS) {
		printk("Failed to read whoami!\n");
		return rc;
	}	
	if (who_am_i != ICM_42607_WHOAMI) {
		printk("Bad WHOAMI value!\n");
		printk("Read 0x%02x, expected 0x%02x\n", who_am_i, ICM_42607_WHOAMI);
		return INV_ERROR;
	}

	return rc;
}

int iotex_icm_chip_init(void)
{
    int rc = 0;
    uint8_t who_am_i;
    static struct inv_icm426xx_serif icm_serif;
    static struct inv_imu_serif icm_serif_42607;
    icm_chip_detect = 0;
    /* ICM42605 i2c bus init */
    if (!(__i2c_dev_icm_chip = device_get_binding(I2C_DEV_ICM_CHIP))) {
        printk("I2C: Device driver[%s] not found.\n", I2C_DEV_ICM_CHIP);
        return -1;
    }
	/* Check WHOAMI */
	rc = i2c_reg_read_byte(__i2c_dev_icm_chip, I2C_ADDR_ICM42607, MPUREG_WHO_AM_I, &who_am_i);
	if(rc != INV_ERROR_SUCCESS) {
        printk("Detect icm-xxx chip error\n");
        return  -1;
    }
    if(who_am_i == ICM_42607_WHOAMI) {
        printk("Detected icm-42607 id: %02x\n",who_am_i);
        rc = setup_icm42607(&icm_serif_42607);
        rc |= inv_imu_womconfig(&icm_driver);
        rc |= inv_imu_set_accel_fsr(&icm_driver, ACCEL_CONFIG0_FS_SEL_4g);
        icm_chip_detect = 2;
        return rc;
    }
    /* ICM42605 init */
    icm_serif.context = 0;  /* no need */
    icm_serif.read_reg = inv_icm426xx_io_hal_read_reg;
    icm_serif.write_reg = inv_icm426xx_io_hal_write_reg;
    icm_serif.max_read  = 1024 * 32;  /* maximum number of bytes allowed per serial read */
    icm_serif.max_write = 1024 * 32;  /* maximum number of bytes allowed per serial write */
    icm_serif.serif_type = ICM42605_SERIF_TYPE;

    rc = inv_icm426xx_init(&__icm_driver, &icm_serif, NULL); 
    rc |= inv_icm426xx_configure_fifo(&__icm_driver, INV_ICM426XX_FIFO_DISABLED);
    if (rc != INV_ERROR_SUCCESS) {
        printk("!!! ERROR : failed to initialize Icm426xx.\n");
        fatalError();
        return rc;
    }

    /* Check WHOAMI */
    rc = inv_icm426xx_get_who_am_i(&__icm_driver, &who_am_i);
    if ((rc != INV_ERROR_SUCCESS) || (who_am_i != ICM_42605_WHOAMI)) {
        printk("Read ICM-42605 ID  error\n");
        return rc;
    }
    printk("ICM42605 WHOAMI value. Got 0x%02x\n", who_am_i);
    womConf();
    icm_chip_detect = 1;
    return   icm426xxConfig(ACT_RAW);
}

struct inv_icm426xx * getICMDriver(void)
{
    return &__icm_driver;
}

static void inv_icm42605_format_data(const uint8_t endian, const uint8_t *in, uint16_t *out)
{
    if (endian == ICM426XX_INTF_CONFIG0_DATA_BIG_ENDIAN)
        *out = (in[0] << 8) | in[1];
    else
        *out = (in[1] << 8) | in[0];
}

static int icm42605_get_sensor_data(iotex_storage_icm *icm42605) {
    int i;
    int status = 0;
    uint8_t int_status;
    uint8_t temperature[2];
    uint8_t accel[ACCEL_DATA_SIZE];
    uint8_t gyro[GYRO_DATA_SIZE];
    uint16_t ftemperature;
    uint16_t faccel[ACCEL_DATA_SIZE / 2];
    uint16_t fgyro[GYRO_DATA_SIZE / 2];

    i2cLock();
    /* Ensure data ready status bit is set */
    status |= inv_icm426xx_read_reg(&__icm_driver, MPUREG_INT_STATUS, 1, &int_status);

    if (status) {
        i2cUnlock();  
        return status;
    }

    if (int_status & BIT_INT_STATUS_DRDY) {

        status = inv_icm426xx_read_reg(&__icm_driver, MPUREG_TEMP_DATA0_UI, TEMP_DATA_SIZE, temperature);
        inv_icm42605_format_data(__icm_driver.endianess_data, temperature, (uint16_t *) & (ftemperature));

        status |= inv_icm426xx_read_reg(&__icm_driver, MPUREG_ACCEL_DATA_X0_UI, ACCEL_DATA_SIZE, accel);
        inv_icm42605_format_data(__icm_driver.endianess_data, &accel[0], (uint16_t *)&faccel[0]);
        inv_icm42605_format_data(__icm_driver.endianess_data, &accel[2], (uint16_t *)&faccel[1]);
        inv_icm42605_format_data(__icm_driver.endianess_data, &accel[4], (uint16_t *)&faccel[2]);

        status |= inv_icm426xx_read_reg(&__icm_driver, MPUREG_GYRO_DATA_X0_UI, GYRO_DATA_SIZE, gyro);
        inv_icm42605_format_data(__icm_driver.endianess_data, &gyro[0], (uint16_t *)&fgyro[0]);
        inv_icm42605_format_data(__icm_driver.endianess_data, &gyro[2], (uint16_t *)&fgyro[1]);
        inv_icm42605_format_data(__icm_driver.endianess_data, &gyro[4], (uint16_t *)&fgyro[2]);

        if ((faccel[0] != INVALID_VALUE_FIFO) && (fgyro[0] != INVALID_VALUE_FIFO)) {

            icm42605->temperature = (ftemperature / 132.48) + 25;

            for (i = 0; i < ARRAY_SIZE(icm42605->gyroscope); i++) {
                icm42605->gyroscope[i] = (int16_t)fgyro[i];
            }

            for (i = 0; i < ARRAY_SIZE(icm42605->accelerometer); i++) {
                icm42605->accelerometer[i] = (int16_t)faccel[i];
            }

        }
        else {
            i2cUnlock(); 
            return -1;
        }
    }
    i2cUnlock(); 
    /*else: Data Ready was not set*/

    return status;
}
/* --------------------------------------------------------------------------------------
 *  Static functions definition
 * -------------------------------------------------------------------------------------- */

static void apply_mounting_matrix(const int32_t matrix[9], int32_t raw[3])
{
	unsigned i;
	int64_t data_q30[3];
	
	for(i = 0; i < 3; i++) {
		data_q30[i] =  ((int64_t)matrix[3*i+0] * raw[0]);
		data_q30[i] += ((int64_t)matrix[3*i+1] * raw[1]);
		data_q30[i] += ((int64_t)matrix[3*i+2] * raw[2]);
	}
	raw[0] = (int32_t)(data_q30[0]>>30);
	raw[1] = (int32_t)(data_q30[1]>>30);
	raw[2] = (int32_t)(data_q30[2]>>30);
}
static int icm42607_get_sensor_data(iotex_storage_icm *icm42607) {
    int status = 0;
    uint8_t int_status;
    uint8_t temperature[2];
    uint8_t accel[ACCEL_DATA_SIZE];
    uint8_t gyro[GYRO_DATA_SIZE];
    int32_t accel_data[ACCEL_DATA_SIZE], gyro_data[GYRO_DATA_SIZE];
    struct inv_imu_device *s = &icm_driver;

	i2cLock();
	/* Ensure data ready status bit is set */
	if ((status |= inv_imu_read_reg(s, INT_STATUS_DRDY, 1, &int_status))) {
        i2cUnlock();
		return status;
    }

	if (int_status & INT_STATUS_DRDY_DATA_RDY_INT_MASK) {
		
		status |= inv_imu_read_reg(s, TEMP_DATA1, 2, &temperature[0]);
		
		if (s->endianness_data == INTF_CONFIG0_DATA_BIG_ENDIAN) {
			icm42607->temperature = (((int16_t)temperature[0]) << 8) | temperature[1];
		} else {
			icm42607->temperature = (((int16_t)temperature[1]) << 8) | temperature[0];
		}
		
		status |= inv_imu_read_reg(s, ACCEL_DATA_X1, ACCEL_DATA_SIZE, &accel[0]);

		if (s->endianness_data == INTF_CONFIG0_DATA_BIG_ENDIAN) {
			accel_data[0] = (accel[0] << 8) | accel[1];
			accel_data[1] = (accel[2] << 8) | accel[3];
			accel_data[2] = (accel[4] << 8) | accel[5];
		} else {
			accel_data[0] = (accel[1] << 8) | accel[0];
			accel_data[1] = (accel[3] << 8) | accel[2];
			accel_data[2] = (accel[5] << 8) | accel[4];
		}
		
		status |= inv_imu_read_reg(s, GYRO_DATA_X1, GYRO_DATA_SIZE, &gyro[0]);
		if (s->endianness_data == INTF_CONFIG0_DATA_BIG_ENDIAN) {
			gyro_data[0] = (gyro[0] << 8) | gyro[1];
			gyro_data[1] = (gyro[2] << 8) | gyro[3];
			gyro_data[2] = (gyro[4] << 8) | gyro[5];
		} else {
			gyro_data[0] = (gyro[1] << 8) | gyro[0];
			gyro_data[1] = (gyro[3] << 8) | gyro[2];
			gyro_data[2] = (gyro[5] << 8) | gyro[4];
		}
    }
    i2cUnlock();
	apply_mounting_matrix(icm_mounting_matrix, accel_data);
	apply_mounting_matrix(icm_mounting_matrix, gyro_data);

    icm42607->accelerometer[0] = accel_data[0];
    icm42607->accelerometer[1] = accel_data[1];
    icm42607->accelerometer[2] = accel_data[2];

    icm42607->gyroscope[0] = gyro_data[0];
    icm42607->gyroscope[1] = gyro_data[1];
    icm42607->gyroscope[2] = gyro_data[2];

    printk("acce:%d,%d,%d, gyro:%d,%d,%d\n",icm42607->accelerometer[0],icm42607->accelerometer[1],icm42607->accelerometer[2],
            icm42607->gyroscope[0],icm42607->gyroscope[1],icm42607->gyroscope[2]);
    
    return status;
}

int  iotex_icm_get_sensor_data(iotex_storage_icm *icm_data) {
    if(icm_chip_detect == 1) {
        return icm42605_get_sensor_data(icm_data);
    }
    else {
        return icm42607_get_sensor_data(icm_data);
    }
}
int iotex_icm_wom_detect(void) {
    if(icm_chip_detect == 1) {
        return womDetect();
    }
    else {
        return inv_icm_wom_detect(&icm_driver);
    }
}


