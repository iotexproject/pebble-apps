#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/drivers/i2c.h>
#include "icm42605/icm426xxDefs.h"
#include "icm42605/icm426xxTransport.h"
#include "icm42605/icm426xxDriver_HL.h"
#include "icm_chip_helper.h"
#include "modem/modem_helper.h"
#include "nvs/local_storage.h"
#include "icm42605/icm426xxActionDetect.h"

extern int tiltConf(void);
extern int womConf(void);
extern int smdConf(void);
extern int rawdataConf(void);

extern int tiltDetect(void);
extern int womDetect(void);
extern int smdDetect(void);
extern int rawdataDetect(void);

const static int (*sensorConfig[MAX_ACT][2])(void) = {
    { tiltConf, tiltDetect },
    { womConf, womDetect },
    { smdConf, smdDetect },
    { rawdataConf, rawdataDetect }
};

int icm426xxConfig(enum SENSOR_ACTION  act)
{
    return (*sensorConfig[act][0])();
}

int icm426xxActionDetect(enum SENSOR_ACTION  act)
{
    return (*sensorConfig[act][1])();
}