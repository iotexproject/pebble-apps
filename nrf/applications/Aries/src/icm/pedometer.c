#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <drivers/i2c.h>
#include <logging/log.h>
#include "Icm426xxDefs.h"
#include "Icm426xxTransport.h"
#include "Icm426xxDriver_HL.h"
#include "icm42605_helper.h"
#include "modem/modem_helper.h"
#include "nvs/local_storage.h"
#include "Icm426xxDriver_HL_apex.h"
#include "pedometer.h"

LOG_MODULE_REGISTER(pedometer, CONFIG_ASSET_TRACKER_LOG_LEVEL);

/* #define DEBUG_PETOMETER */

#ifdef DEBUG_PETOMETER
uint8_t testPrint[300];
#endif

static uint8_t dmp_odr_hz;

struct _PEDOMETER  pedometer;

int pedometerConfig(struct inv_icm426xx *icm_driver, ICM426XX_APEX_CONFIG0_DMP_ODR_t pedometer_freq,
					ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_t power_mode,
					ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_t pedo_amp_th,
					uint8_t pedo_step_cnt_th,
					ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_t pedo_sb_timer_th,
					uint8_t pedo_step_det_th,
					ICM426XX_APEX_CONFIG9_SENSITIVITY_MODE_t sensitivity_mode)
{
	int rc = 0;
	ICM426XX_ACCEL_CONFIG0_ODR_t acc_freq;
	inv_icm426xx_apex_parameters_t apex_inputs;
	inv_icm426xx_interrupt_parameter_t config_int = {(inv_icm426xx_interrupt_value)0};
	
	pedometer.cadence_step_per_sec = 0;
	pedometer.step_cnt = 0;
	pedometer.discard_cnt = 0;
	pedometer.classType = PEDOMETER_STOP;
	pedometer.sportTime = 0;
	sys_mutex_init(&pedometer.pedometerMutex);
	
	/* Accel frequency should be at least or higher than the Pedometer frequency to properly running the APEX feature */
	switch(pedometer_freq)
	{
		case ICM426XX_APEX_CONFIG0_DMP_ODR_25Hz:
			acc_freq = ICM426XX_ACCEL_CONFIG0_ODR_25_HZ;
			dmp_odr_hz = 25;
			break;

		case ICM426XX_APEX_CONFIG0_DMP_ODR_50Hz:
			acc_freq = ICM426XX_ACCEL_CONFIG0_ODR_50_HZ;
			dmp_odr_hz = 50;
			break;

		default:
			return INV_ERROR_BAD_ARG;
	}

	/* Enable accelerometer to feed the APEX Pedometer algorithm */
	rc |= inv_icm426xx_set_accel_frequency(icm_driver, acc_freq);

	/* Set 1x averaging, in order to minimize power consumption (16x by default) */
	rc |= inv_icm426xx_set_accel_lp_avg(icm_driver, ICM426XX_GYRO_ACCEL_CONFIG0_ACCEL_FILT_AVG_1);

	rc |= inv_icm426xx_enable_accel_low_power_mode(icm_driver);

	/* Get the default parameters for the APEX features */
	rc |= inv_icm426xx_init_apex_parameters_struct(icm_driver, &apex_inputs);
	
	/* Configure the programmable parameters */
	apex_inputs.pedo_amp_th      = pedo_amp_th;
	apex_inputs.pedo_step_cnt_th = pedo_step_cnt_th;
	apex_inputs.pedo_sb_timer_th = pedo_sb_timer_th;
	apex_inputs.pedo_step_det_th = pedo_step_det_th; 
	apex_inputs.sensitivity_mode = sensitivity_mode;
		
	/* Configure the programmable parameter for Low Power mode (WoM+Pedometer) or Normal mode */
	if (power_mode == ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_EN) {
		/* Configure WOM to compare current sample with the previous sample and to produce signal when all axis exceed 52 mg */
		rc |= inv_icm426xx_configure_smd_wom(icm_driver, 
			ICM426XX_DEFAULT_WOM_THS_MG, 
			ICM426XX_DEFAULT_WOM_THS_MG, 
			ICM426XX_DEFAULT_WOM_THS_MG, 
			ICM426XX_SMD_CONFIG_WOM_INT_MODE_ANDED,
			ICM426XX_SMD_CONFIG_WOM_MODE_CMP_PREV);
		
		/* Enable WOM to wake-up the DMP once it goes in power save mode */
		rc |= inv_icm426xx_enable_wom(icm_driver); /* Enable WOM and disable fifo threshold */
		
		apex_inputs.power_save = ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_EN;
	} else 
		apex_inputs.power_save = ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_DIS;

	/* Initializes APEX features */
	rc |= inv_icm426xx_configure_apex_parameters(icm_driver, &apex_inputs);
	rc |= inv_icm426xx_set_apex_frequency(icm_driver, pedometer_freq);

	/* Enable the pedometer */
	rc |= inv_icm426xx_enable_apex_pedometer(icm_driver);

	/* Disable fifo threshold, data ready and WOM interrupts INT1 */
	inv_icm426xx_get_config_int1(icm_driver, &config_int);
	config_int.INV_ICM426XX_FIFO_THS = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_UI_DRDY = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_WOM_X = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_WOM_Y = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_WOM_Z = INV_ICM426XX_DISABLE;
	inv_icm426xx_set_config_int1(icm_driver, &config_int);
	
	/* Disable fifo threshold, data ready and WOM interrupts IBI */
	inv_icm426xx_get_config_ibi(icm_driver, &config_int);
	config_int.INV_ICM426XX_FIFO_THS = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_UI_DRDY = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_WOM_X = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_WOM_Y = INV_ICM426XX_DISABLE;
	config_int.INV_ICM426XX_WOM_Z = INV_ICM426XX_DISABLE;
	inv_icm426xx_set_config_ibi(icm_driver, &config_int);
	return rc;
}


int readSteps(struct inv_icm426xx *icm_driver)
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
	rc = inv_icm426xx_read_reg(icm_driver, MPUREG_INT_STATUS2, 1, &int_status2);
	if(rc != INV_ERROR_SUCCESS)
		goto IO_ERR_STOP;	
	
	if(int_status2 & (BIT_INT_STATUS2_WOM_X_INT|BIT_INT_STATUS2_WOM_Y_INT|BIT_INT_STATUS2_WOM_Z_INT)){
		sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
		pedometer.pedometerWOM = 1;
		sys_mutex_unlock(&pedometer.pedometerMutex);
	}	
	/* 
	 * Read Pedometer interrupt status
	 */
	rc |= inv_icm426xx_read_reg(icm_driver, MPUREG_INT_STATUS3, 1, &int_status3);
	if(rc != INV_ERROR_SUCCESS)
		goto IO_ERR_STOP;
	if(int_status3 & (BIT_INT_STATUS3_STEP_DET)) {
		
		step_cnt_ovflw = (int_status3 & BIT_INT_STATUS3_STEP_CNT_OVFL) ? 1 : 0;
		inv_icm426xx_apex_step_activity_t apex_data0;

		rc |= inv_icm426xx_get_apex_data_activity(icm_driver, &apex_data0);
		
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
		case ICM426XX_APEX_DATA3_ACTIVITY_CLASS_WALK:
#ifdef DEBUG_PETOMETER		
			sprintf(testPrint,"%d steps - cadence: %.2f steps/sec - WALK\n",  (uint32_t)step_cnt, cadence_step_per_sec);
			printk(testPrint);
#endif			
			pedometer.classType = PEDOMETER_WALK;
			pedometer.cadence_step_per_sec = (uint32_t)(cadence_step_per_sec*100);
			pedometer.step_cnt = step_cnt - pedometer.discard_cnt;			
			break;
		case ICM426XX_APEX_DATA3_ACTIVITY_CLASS_RUN:
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
	/* clear step_cnt every day 00:00:00 ~ 00:01:00 */
	localTime = iotex_modem_get_local_time();
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
bool isPedometerActive(void) {
	bool ret;

	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret = (pedometer.classType == PEDOMETER_WALK) || (pedometer.classType == PEDOMETER_RUN);
	sys_mutex_unlock(&pedometer.pedometerMutex);
	return ret;
}

bool isPedometerStop(void)
{
	bool ret;

	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret = (pedometer.classType == PEDOMETER_STOP);
	sys_mutex_unlock(&pedometer.pedometerMutex);
	return ret;	
}
bool isPedometerWOM(void)
{
	bool ret;

	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret = (pedometer.pedometerWOM == 1);
	pedometer.pedometerWOM = 0;
	sys_mutex_unlock(&pedometer.pedometerMutex);	
	return ret;
}
uint32_t getCalK(void)
{
	uint32_t ret;
	static enum PEDOMETER_CLASS_TYPE activeAction = PEDOMETER_WALK;
	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	if((pedometer.classType == PEDOMETER_WALK) || (pedometer.classType == PEDOMETER_RUN)){
		activeAction = pedometer.classType;
	}
	if(activeAction == PEDOMETER_WALK)
		ret = 8214;
	else if(activeAction == PEDOMETER_RUN)
		ret = 10360;
	else
		ret = 0;
	sys_mutex_unlock(&pedometer.pedometerMutex);
	return ret;
}

enum PEDOMETER_CLASS_TYPE readPedometer(uint32_t *pace, uint64_t *steps, uint32_t *time)
{
	bool ret;
	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret = (pedometer.classType == PEDOMETER_WALK) || (pedometer.classType == PEDOMETER_RUN);
	*pace  = pedometer.cadence_step_per_sec;
	*steps = pedometer.step_cnt;
	*time = pedometer.sportTime;
	sys_mutex_unlock(&pedometer.pedometerMutex);
	return ret;
}
bool stepsComp(uint64_t steps, enum PEDOMETER_CLASS_TYPE type)
{
	bool ret;
	uint64_t stp = steps;
	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret =  ((stp == pedometer.step_cnt) && (type == pedometer.classType));
	sys_mutex_unlock(&pedometer.pedometerMutex);
	return ret;
}

uint64_t getSteps(void)
{
	uint64_t  ret;
	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret =  pedometer.step_cnt;
	sys_mutex_unlock(&pedometer.pedometerMutex);	
	return ret;
}
enum PEDOMETER_CLASS_TYPE getType(void)
{
	enum PEDOMETER_CLASS_TYPE  ret;
	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	ret =  pedometer.classType;
	sys_mutex_unlock(&pedometer.pedometerMutex);	
	return ret;
}

void sportTiming(void)
{
	sys_mutex_lock(&pedometer.pedometerMutex, K_FOREVER);
	if((pedometer.classType == PEDOMETER_WALK) || (pedometer.classType == PEDOMETER_RUN))
		pedometer.sportTime++;
	sys_mutex_unlock(&pedometer.pedometerMutex);
}


