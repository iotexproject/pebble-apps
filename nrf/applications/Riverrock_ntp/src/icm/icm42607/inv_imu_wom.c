#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <drivers/i2c.h>
#include <logging/log.h>
#include "icm42607/inv_imu_defs.h"
#include "icm42607/inv_imu_transport.h"
#include "icm42607/inv_imu_driver.h"
#include "icm_chip_helper.h"
#include "modem/modem_helper.h"
#include "nvs/local_storage.h"
#include "icm42607/inv_imu_apex.h"


LOG_MODULE_REGISTER(inv_imu_wom, CONFIG_ASSET_TRACKER_LOG_LEVEL);

/* Initial WOM threshold to be applied to ICM in mg */
#define WOM_THRESHOLD_INITIAL_MG 200

/* WOM threshold to be applied to ICM, ranges from 1 to 255, in 4mg unit */
#define  WOM_THRESHOLD  (WOM_THRESHOLD_INITIAL_MG / 16)


int  inv_imu_womconfig(struct inv_imu_device *icm_driver)
{
	int rc = 0;

	/* Disabling FIFO to avoid extra power consumption due to ALP config */
	rc |= inv_imu_configure_fifo(icm_driver, INV_IMU_FIFO_DISABLED);
	
	rc |= inv_imu_set_accel_frequency(icm_driver, ACCEL_CONFIG0_ODR_800_HZ);
	
	/* Set 2x averaging, in order to minimize power consumption (16x by default) */
	rc |= inv_imu_set_accel_lp_avg(icm_driver, ACCEL_CONFIG1_ACCEL_FILT_AVG_2);
	//rc |= inv_imu_enable_accel_low_power_mode(icm_driver);
	rc |= inv_imu_enable_accel_low_noise_mode(icm_driver);
	rc |= inv_imu_enable_gyro_low_noise_mode(icm_driver);
	
	/* Configure WOM to produce signal when at least one axis exceed 200 mg */
	rc |= inv_imu_configure_wom(icm_driver,WOM_THRESHOLD, WOM_THRESHOLD, WOM_THRESHOLD,
								WOM_CONFIG_WOM_INT_MODE_ORED,WOM_CONFIG_WOM_INT_DUR_1_SMPL);
	
	rc |= inv_imu_enable_wom(icm_driver);

	if (rc)
		printk("Error while configuring WOM threshold to initial value %c", WOM_THRESHOLD);
	
	return rc;
}

int  inv_icm_wom_detect(struct inv_imu_device *icm_driver) {
	uint8_t int_status;
	int rc = 0;	
	/*
	 *  Read WOM interrupt status
	 */
	i2cLock();
	rc = inv_imu_read_reg(icm_driver, INT_STATUS2, 1, &int_status);
	i2cUnlock();
	if (rc != INV_ERROR_SUCCESS) {
        printk("error wom status read : 0x%x\n",int_status);
        sys_reboot(0);
        return  0;
	}

	if (int_status & ( INT_STATUS2_WOM_X_INT_MASK | INT_STATUS2_WOM_Y_INT_MASK | INT_STATUS2_WOM_Z_INT_MASK )) {
		return 1;
	}
}
