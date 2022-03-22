#ifndef _IOTEX_MODEM_H_
#define _IOTEX_MODEM_H_

#define MODEM_IMEI_LEN 15
#define TIMESTAMP_STR_LEN 50

typedef struct {
    char data[TIMESTAMP_STR_LEN];
} __attribute__((packed)) iotex_st_timestamp;

const char *iotex_modem_get_imei();

int iotex_model_get_signal_quality();

const char *iotex_modem_get_clock(iotex_st_timestamp *stamp);

double iotex_modem_get_clock_raw(iotex_st_timestamp *stamp);

uint32_t iotex_modem_get_battery_voltage(void);

void CheckPower(void);

uint8_t* ReadDataFromModem(uint32_t sec, uint8_t *buf, uint32_t len);

bool WritDataIntoModem(uint32_t sec, uint8_t *str);

void getSysInfor(uint8_t *buf);

uint32_t iotex_modem_get_local_time(void);

#endif /* _IOTEX_MODEM_H_ */