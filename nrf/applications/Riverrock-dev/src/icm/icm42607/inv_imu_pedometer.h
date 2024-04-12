#ifndef    __IN_IMU_PEDOMETER_H__
#define    __IN_IMU_PEDOMETER_H__
#include <zephyr/sys/mutex.h>
#include "icm42607/inv_imu_defs.h"
#include <stdint.h>
#include "icm42607/inv_imu_transport.h"
#include "icm42607/inv_imu_defs.h"
#include "icm42607/inv_imu_driver.h"
#include "icm42607/inv_imu_apex.h"
#include "icm_chip_helper.h"

/*
 * Pedometer frequency 
 * Use type APEX_CONFIG1_DMP_ODR_t to define pedometer frequency
 * These types are defined in inv_imu_defs.h.
 *
 * \note The frequency modes to run the Pedometer are:
 * APEX_CONFIG1_DMP_ODR_25Hz  (Low Power mode), 
 * APEX_CONFIG1_DMP_ODR_50Hz  (Normal mode)
 */
#define PEDOMETER_FREQUENCY_MODE	        APEX_CONFIG1_DMP_ODR_25Hz

/*
 * Pedometer power save mode
 * Use type APEX_CONFIG0_DMP_POWER_SAVE_t to define pedometer power save mode
 * These types are defined in inv_imu_defs.h.
 */
#define PEDOMETER_POWER_SAVE_MODE           APEX_CONFIG0_DMP_POWER_SAVE_EN

/* 
 * Peak threshold value to be considered as a valid step (mg) 
 * Use type APEX_CONFIG3_PEDO_AMP_TH_t to define the valid step peak threshold
 * These types are defined in inv_imu_defs.h
 */
#define PEDOMETER_VALID_STEP_THRESHOLD      APEX_CONFIG3_PEDO_AMP_TH_62_MG 	
		
/*
 * Minimum number of steps that must be detected before the pedometer step count begins incrementing 
 */
#define PEDOMETER_STEP_COUNTER_THRESHOLD    5

/* 
 * Duration of non-walk in number of samples to exit the current walk mode.
 * PEDOMETER_STEP_COUNTER_THRESHOLD number of steps above must again be detected before step count starts to increase
 * Use type APEX_CONFIG4_PEDO_SB_TIMER_TH_t to define the non-walk duration
 * These types are defined in inv_imu_defs.h
 *
 * \note The recommended values according to the frequency mode selected are :
 * APEX_CONFIG4_PEDO_SB_TIMER_TH_100_SAMPLES in Low Power mode
 * APEX_CONFIG4_PEDO_SB_TIMER_TH_150_SAMPLES in Normal mode
 */
#define PEDOMETER_NON_WALK_DURATION         APEX_CONFIG4_PEDO_SB_TIMER_TH_100_SAMPLES

/* 
 * Minimum number of low latency steps that must be detected before the pedometer step count begins incrementing 
 */
#define PEDOMETER_STEP_DETECTOR_THRESHOLD   2

/*
 * Sensitivity mode: Normal or Slow walk
 * Use type APEX_CONFIG9_SENSITIVITY_MODE_t to define sensitivity mode 
 * These types are defined in inv_imu_defs.h.
 */
#define PEDOMETER_SENSITIVITY_MODE	        APEX_CONFIG9_SENSITIVITY_MODE_NORMAL

/* WOM threshold to be applied to IMU, ranges from 1 to 255, in 4mg unit */
#define INV_IMU_WOM_THRESHOLD_INITIAL_MG   3   /* = 3 * 1000 / 256 = 11.7 mg */


int inv_imu_pedometerConf(struct inv_imu_device *icm_driver);
int inv_imu_readSteps(struct inv_icm426xx *icm_driver);


#endif