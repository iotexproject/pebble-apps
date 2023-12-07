#ifndef    __PEDOMETER_H__
#define    __PEDOMETER_H__
#include "icm42605/icm426xxDefs.h"
#include "icm_chip_helper.h"

/*
 * Pedometer frequency 
 * Use type ICM426XX_APEX_CONFIG0_DMP_ODR_t to define pedometer frequency
 * These types are defined in icm426xxDefs.h.
 *
 * \note The frequency modes to run the Pedometer are :
 * ICM426XX_APEX_CONFIG0_DMP_ODR_25Hz  (Low Power mode), 
 * ICM426XX_APEX_CONFIG0_DMP_ODR_50Hz  (Normal mode)
 */
#define ICM_PEDOMETER_FREQUENCY_MODE  ICM426XX_APEX_CONFIG0_DMP_ODR_25Hz

/*
 * Pedometer power save mode
 * Use type ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_t to define pedometer power save mode
 * These types are defined in icm426xxDefs.h.
 */
#define ICM_PEDOMETER_POWER_SAVE_MODE ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_EN
 
/*
 * Pedometer programmable parameters 
 */
 
/* 
 * Peak threshold value to be considered as a valid step (mg) 
 * Use type ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_t to define the valid step peak threshold
 * These types are defined in icm426xxDefs.h
 */
#define ICM_PEDOMETER_VALID_STEP_THRESHOLD /*ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_62MG*/  ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_30MG
		
/*
 * Minimum number of steps that must be detected before the pedometer step count begins incrementing 
 */
#define ICM_PEDOMETER_STEP_COUNTER_THRESHOLD 2

/* 
 * Duration of non-walk in number of samples to exit the current walk mode.
 * ICM_PEDOMETER_STEP_COUNTER_THRESHOLD number of steps above must again be detected before step count starts to increase
 * Use type ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_t to define the non-walk duration
 * These types are defined in icm426xxDefs.h
 *
 * \note The recommended values according to the frequency mode selected are :
 * ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_100_SAMPLES in Low Power mode
 * ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_150_SAMPLES in Normal mode
 */
#define ICM_PEDOMETER_NON_WALK_DURATION ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_100_SAMPLES

/* 
 * Minimum number of low latency steps that must be detected before the pedometer step count begins incrementing 
 */
#define ICM_PEDOMETER_STEP_DETECTOR_THRESHOLD 2

/*
 * Sensitivity mode: Normal
 * Use type ICM426XX_APEX_CONFIG9_SENSITIVITY_MODE_t to define sensitivity mode 
 * These types are defined in icm426xxDefs.h.
 */
#define ICM_PEDOMETER_SENSITIVITY_MODE ICM426XX_APEX_CONFIG9_SENSITIVITY_MODE_NORMAL


int pedometerConfig(struct inv_icm426xx *icm_driver, ICM426XX_APEX_CONFIG0_DMP_ODR_t pedometer_freq,
					ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_t power_mode,
					ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_t pedo_amp_th,
					uint8_t pedo_step_cnt_th,
					ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_t pedo_sb_timer_th,
					uint8_t pedo_step_det_th,
					ICM426XX_APEX_CONFIG9_SENSITIVITY_MODE_t sensitivity_mode);

int readSteps(struct inv_icm426xx *icm_driver);



#endif