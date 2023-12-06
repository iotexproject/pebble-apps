#ifndef _IOTEX_ICM42605_H_
#define _IOTEX_ICM42605_H_

#include <stdint.h>
#include <zephyr/sys/mutex.h>
#include "nvs/local_storage.h"


#if(CONFIG_IOTEX_BOARD_VERSION == 3)
#define I2C_DEV_ICM_CHIP  "i2c2"
#endif
#if(CONFIG_IOTEX_BOARD_VERSION == 2)
#define I2C_DEV_ICM42605  "I2C_1"
#endif

#define I2C_ADDR_ICM42605 0x69

#define I2C_ADDR_ICM42607 0x69

#define IS_LOW_NOISE_MODE 1
#define TMST_CLKIN_32K 0

#define ICM42605_SERIF_TYPE ICM426XX_UI_I2C
#define ICM42607_SERIF_TYPE UI_I2C


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

int iotex_icm_chip_init(void);
int iotex_icm_get_sensor_data(iotex_storage_icm *icm42605);
int  inv_icm_wom_detect(struct inv_imu_device *icm_driver);


/* For icm426xxDriver_HL using */
void inv_icm426xx_sleep_us(uint32_t us);
uint64_t inv_icm426xx_get_time_us(void);
struct inv_icm426xx * getICMDriver(void);
int iotexReadSteps(void);

uint32_t getCalK(void);
bool isPedometerActive(void);
void setPedometerStop(void);
enum PEDOMETER_CLASS_TYPE readPedometer(uint32_t *pace, uint64_t *steps, uint32_t *time);

bool isPedometerWOM(void);
uint64_t getSteps(void);
bool stepsComp(uint64_t steps,enum PEDOMETER_CLASS_TYPE type);
enum PEDOMETER_CLASS_TYPE getType(void);
void sportTiming(void);

int  inv_imu_womconfig(struct inv_imu_device *icm_driver);

#endif
