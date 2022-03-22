#ifndef    __PEDOMETER_H__
#define    __PEDOMETER_H__
#include <sys/mutex.h>
#include "Icm426xxDefs.h"

/*
 * Pedometer frequency 
 * Use type ICM426XX_APEX_CONFIG0_DMP_ODR_t to define pedometer frequency
 * These types are defined in Icm426xxDefs.h.
 *
 * \note The frequency modes to run the Pedometer are :
 * ICM426XX_APEX_CONFIG0_DMP_ODR_25Hz  (Low Power mode), 
 * ICM426XX_APEX_CONFIG0_DMP_ODR_50Hz  (Normal mode)
 */
#define ICM_PEDOMETER_FREQUENCY_MODE ICM426XX_APEX_CONFIG0_DMP_ODR_25Hz

/*
 * Pedometer power save mode
 * Use type ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_t to define pedometer power save mode
 * These types are defined in Icm426xxDefs.h.
 */
#define ICM_PEDOMETER_POWER_SAVE_MODE ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_EN
 
/*
 * Pedometer programmable parameters 
 */
 
/* 
 * Peak threshold value to be considered as a valid step (mg) 
 * Use type ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_t to define the valid step peak threshold
 * These types are defined in Icm426xxDefs.h
 */
#define ICM_PEDOMETER_VALID_STEP_THRESHOLD ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_62MG
		
/*
 * Minimum number of steps that must be detected before the pedometer step count begins incrementing 
 */
#define ICM_PEDOMETER_STEP_COUNTER_THRESHOLD 5

/* 
 * Duration of non-walk in number of samples to exit the current walk mode.
 * ICM_PEDOMETER_STEP_COUNTER_THRESHOLD number of steps above must again be detected before step count starts to increase
 * Use type ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_t to define the non-walk duration
 * These types are defined in Icm426xxDefs.h
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
 * These types are defined in Icm426xxDefs.h.
 */
#define ICM_PEDOMETER_SENSITIVITY_MODE ICM426XX_APEX_CONFIG9_SENSITIVITY_MODE_NORMAL

enum PEDOMETER_CLASS_TYPE{
	PEDOMETER_STOP = 0,
	PEDOMETER_WALK,
	PEDOMETER_RUN,
};

enum PEDOMETER_STATUS{
	NOT_MOVED,
	MOVED,
};


struct _PEDOMETER{
    uint32_t  cadence_step_per_sec;
    uint64_t  step_cnt;
	uint64_t  discard_cnt;
    enum PEDOMETER_CLASS_TYPE  classType;
	struct sys_mutex  pedometerMutex;
	uint32_t  pedometerWOM;
	uint32_t  startTime;
	uint32_t  sportTime;
};


int pedometerConfig(struct inv_icm426xx *icm_driver, ICM426XX_APEX_CONFIG0_DMP_ODR_t pedometer_freq,
					ICM426XX_APEX_CONFIG0_DMP_POWER_SAVE_t power_mode,
					ICM426XX_APEX_CONFIG2_PEDO_AMP_TH_t pedo_amp_th,
					uint8_t pedo_step_cnt_th,
					ICM426XX_APEX_CONFIG3_PEDO_SB_TIMER_TH_t pedo_sb_timer_th,
					uint8_t pedo_step_det_th,
					ICM426XX_APEX_CONFIG9_SENSITIVITY_MODE_t sensitivity_mode);

int readSteps(struct inv_icm426xx *icm_driver);
bool isPedometerActive(void);
void setPedometerStop(void);
enum PEDOMETER_CLASS_TYPE readPedometer(uint32_t *pace, uint64_t *steps, uint32_t *time);
uint32_t getCalK(void);
bool isPedometerWOM(void);
uint64_t getSteps(void);
bool stepsComp(uint64_t steps,enum PEDOMETER_CLASS_TYPE type);
enum PEDOMETER_CLASS_TYPE getType(void);
void sportTiming(void);


#endif