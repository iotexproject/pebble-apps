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
#include "icm42607/inv_imu_pedometer.h"

LOG_MODULE_REGISTER(inv_imu_pedometer, CONFIG_ASSET_TRACKER_LOG_LEVEL);

/* #define DEBUG_PETOMETER */

#ifdef DEBUG_PETOMETER
uint8_t testPrint[300];
#endif

static uint8_t dmp_odr_hz;

extern struct _PEDOMETER  pedometer;

int inv_imu_pedometerConf(struct inv_imu_device *icm_driver)
{
	int rc = 0;
	ACCEL_CONFIG0_ODR_t acc_freq;
	inv_imu_apex_parameters_t apex_inputs;
	inv_imu_interrupt_parameter_t config_int = {(inv_imu_interrupt_value)0};

	pedometer.cadence_step_per_sec = 0;
	pedometer.step_cnt = 0;
	pedometer.discard_cnt = 0;
	pedometer.classType = PEDOMETER_STOP;
	pedometer.sportTime = 0;
	sys_mutex_init(&pedometer.pedometerMutex);

	/* Disabling FIFO to avoid extra power consumption due to ALP config */
	rc |= inv_imu_configure_fifo(icm_driver, INV_IMU_FIFO_DISABLED);
	
	/* Accel frequency should be at least or higher 
	 * than the Pedometer frequency to properly running the APEX feature 
	 */
	switch (PEDOMETER_FREQUENCY_MODE) {
	case APEX_CONFIG1_DMP_ODR_25Hz:
		acc_freq = ACCEL_CONFIG0_ODR_25_HZ;
		dmp_odr_hz = 25;
		break;
	case APEX_CONFIG1_DMP_ODR_50Hz:
		acc_freq = ACCEL_CONFIG0_ODR_50_HZ;
		dmp_odr_hz = 50;
		break;
	default:
		return INV_ERROR_BAD_ARG;
	}

	/* Enable accelerometer to feed the APEX Pedometer algorithm */
	rc |= inv_imu_set_accel_frequency(icm_driver, acc_freq);

	/* Set 2x averaging, in order to minimize power consumption (16x by default) */
	rc |= inv_imu_set_accel_lp_avg(icm_driver, ACCEL_CONFIG1_ACCEL_FILT_AVG_2);

	rc |= inv_imu_enable_accel_low_power_mode(icm_driver);

	/* Get the default parameters for the APEX features */
	rc |= inv_imu_apex_init_parameters_struct(icm_driver, &apex_inputs);

	/* Configure the programmable parameters */
	apex_inputs.pedo_amp_th      = PEDOMETER_VALID_STEP_THRESHOLD;
	apex_inputs.pedo_step_cnt_th = PEDOMETER_STEP_COUNTER_THRESHOLD;
	apex_inputs.pedo_sb_timer_th = PEDOMETER_NON_WALK_DURATION;
	apex_inputs.pedo_step_det_th = PEDOMETER_STEP_DETECTOR_THRESHOLD; 
	apex_inputs.sensitivity_mode = PEDOMETER_SENSITIVITY_MODE;

	/* Configure the programmable parameter for Low Power mode (WoM+Pedometer) or Normal mode */
	if (PEDOMETER_POWER_SAVE_MODE == APEX_CONFIG0_DMP_POWER_SAVE_EN) {
		/* config wom threshold */
		rc |= inv_imu_configure_wom(icm_driver,INV_IMU_WOM_THRESHOLD_INITIAL_MG, INV_IMU_WOM_THRESHOLD_INITIAL_MG, 
									INV_IMU_WOM_THRESHOLD_INITIAL_MG,WOM_CONFIG_WOM_INT_MODE_ANDED,WOM_CONFIG_WOM_INT_DUR_1_SMPL);
		/* Enable WOM to wake-up the DMP once it goes in power save mode */
		rc |= inv_imu_enable_wom(icm_driver); /* Enable WOM and disable fifo threshold */
		apex_inputs.power_save = APEX_CONFIG0_DMP_POWER_SAVE_EN;
	} else {
		apex_inputs.power_save = APEX_CONFIG0_DMP_POWER_SAVE_DIS;
	}

	/* Initializes APEX features */
	rc |= inv_imu_apex_configure_parameters(icm_driver, &apex_inputs);
	rc |= inv_imu_apex_set_frequency(icm_driver, PEDOMETER_FREQUENCY_MODE);

	/* Enable the pedometer */
	rc |= inv_imu_apex_enable_pedometer(icm_driver);

	/* Disable fifo threshold, data ready and WOM interrupts INT1 */
	rc |= inv_imu_get_config_int1(icm_driver, &config_int);
	config_int.INV_FIFO_THS = INV_IMU_DISABLE;
	config_int.INV_UI_DRDY = INV_IMU_DISABLE;
	config_int.INV_WOM_X = INV_IMU_DISABLE;
	config_int.INV_WOM_Y = INV_IMU_DISABLE;
	config_int.INV_WOM_Z = INV_IMU_DISABLE;
	rc |= inv_imu_set_config_int1(icm_driver, &config_int);

	return rc;
}


int inv_imu_readSteps(struct inv_icm426xx *icm_driver)
{
	uint8_t int_status2, int_status3;
	uint64_t irq_timestamp = 0;
	int rc;
	float cadence_step_per_sec = 0;
	float nb_samples = 0;
	static uint32_t  stopCounter=0;
	uint32_t localTime = 0;
	static uint64_t step_cnt = 0;
	uint8_t step_cnt_ovflw = 0;

	sportTiming();
	/* 
	 * Read WOM interrupt status 
	 */
	rc = inv_imu_read_reg(icm_driver, INT_STATUS2, 1, &int_status2);
	if(rc != INV_ERROR_SUCCESS)
		goto IO_ERR_STOP;	
	
	if(int_status2 & (INT_STATUS2_WOM_X_INT_MASK|INT_STATUS2_WOM_Y_INT_MASK|INT_STATUS2_WOM_Z_INT_MASK)){
		sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
		pedometer.pedometerWOM = 1;
		sys_mutex_unlock(&pedometer.pedometerMutex);
	}	
	/* 
	 * Read Pedometer interrupt status
	 */
	rc = inv_imu_read_reg(icm_driver, INT_STATUS3, 1, &int_status3);
	if(rc != INV_ERROR_SUCCESS)
		goto IO_ERR_STOP;
	if(int_status3 & (INT_STATUS3_STEP_DET_INT_MASK)) {

		step_cnt_ovflw = (int_status3 & INT_STATUS3_STEP_CNT_OVF_INT_MASK) ? 1 : 0;
		inv_imu_apex_step_activity_t apex_data0;

		rc |= inv_imu_apex_get_data_activity(icm_driver, &apex_data0);
		
		/* Converting u6.2 to float */
		nb_samples = (apex_data0.step_cadence >> 2) + (float)(apex_data0.step_cadence & 0x03)*0.25;
		cadence_step_per_sec = (float) dmp_odr_hz / nb_samples;

		if(rc != INV_ERROR_SUCCESS)
			goto IO_ERR_STOP;
		
		if(step_cnt == apex_data0.step_cnt + step_cnt_ovflw * (uint64_t)UINT16_MAX)
			return rc;
		
		step_cnt = apex_data0.step_cnt + step_cnt_ovflw * UINT16_MAX;
		stopCounter = 0;
		sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
		switch(apex_data0.activity_class)
		{
		case APEX_DATA3_ACTIVITY_CLASS_WALK:
#ifdef DEBUG_PETOMETER		
			sprintf(testPrint,"%d steps - cadence: %.2f steps/sec - WALK\n",  (uint32_t)step_cnt, cadence_step_per_sec);
			printk(testPrint);
#endif			
			pedometer.classType = PEDOMETER_WALK;
			pedometer.cadence_step_per_sec = (uint32_t)(cadence_step_per_sec*100);
			pedometer.step_cnt = step_cnt - pedometer.discard_cnt;			
			break;
		case APEX_DATA3_ACTIVITY_CLASS_RUN:
#ifdef DEBUG_PETOMETER			
			sprintf(testPrint,"%d steps - cadence: %.2f steps/sec - RUN \n", (uint32_t)step_cnt, cadence_step_per_sec);
			printk(testPrint);
#endif			
			pedometer.classType = PEDOMETER_RUN;
			pedometer.cadence_step_per_sec = (uint32_t)(cadence_step_per_sec*100);
			pedometer.step_cnt = step_cnt- pedometer.discard_cnt;
			break;
		default:
#ifdef DEBUG_PETOMETER			
			sprintf(testPrint," %d steps - cadence: %.2f steps/sec \n", (uint32_t)step_cnt, cadence_step_per_sec);
			printk(testPrint);
#endif			
			pedometer.cadence_step_per_sec = (uint32_t)(cadence_step_per_sec*100);
			pedometer.step_cnt = step_cnt- pedometer.discard_cnt;
			pedometer.classType = PEDOMETER_WALK;
			break;
		}
		sys_mutex_unlock(&pedometer.pedometerMutex);
	}
	else
	{
IO_ERR_STOP:
		//stopCounter++;
		//if(stopCounter > 3) {
			sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
			pedometer.classType = PEDOMETER_STOP;
			sys_mutex_unlock(&pedometer.pedometerMutex);
		//}
	}
	/* clear step_cnt every day 01:00:00 ~ 01:01:00 */
	localTime = iotex_modem_get_local_time();
	/*printk("localTime : 0x%04X, %02d:%02d:%02d\n", localTime, (localTime&0x00FF0000)>>16, (localTime&0x0000FF00)>>8, (localTime&0x000000FF));*/
	if((localTime > 0x010000) && (localTime < 0x010100)){
		sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
		if((pedometer.classType != PEDOMETER_WALK) && (pedometer.classType != PEDOMETER_RUN)) {
			pedometer.discard_cnt = step_cnt;
			pedometer.cadence_step_per_sec = 0;
			pedometer.step_cnt = 0;
			pedometer.discard_cnt = 0;
			pedometer.classType = PEDOMETER_STOP;
			pedometer.sportTime = 0;
		}
		sys_mutex_unlock(&pedometer.pedometerMutex);
	}
	return rc;
}