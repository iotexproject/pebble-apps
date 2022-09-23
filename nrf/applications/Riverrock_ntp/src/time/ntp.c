#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <power/reboot.h>
#include <logging/log_ctrl.h>
#include <sys/mutex.h>
#include <modem/lte_lc.h>
#include <logging/log.h>


LOG_MODULE_REGISTER(ntp, 4);

static K_SEM_DEFINE(date_time_obtained, 0, 1);
struct sys_mutex ntp_mutex;
static s64_t ntp_timestamp = 0, sys_timestamp = 0;
static s64_t sys_tick;

extern struct time_aux {
	int64_t date_time_utc;
	int64_t last_date_time_update;
};
extern int get_date_time_raw(struct time_aux *aux1, struct time_aux *aux2);

struct time_aux  sys_time_aux ={
    0,0
};

int initNTP(void) {
    sys_mutex_init(&ntp_mutex);
    date_time_clear();
    return  0;
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
    k_sem_give(&date_time_obtained);
}

int readNTP(void) {
    int64_t uptime_now;
    struct time_aux  aux1, aux2;
    uint32_t modem_timestamp;
    int ret = 0;

    date_time_clear();
    date_time_update_async(date_time_event_handler);

    k_sem_take(&date_time_obtained, K_FOREVER);

    if(!get_date_time_raw(&aux1, &aux2)) {
        uptime_now = k_uptime_get();
        modem_timestamp = atoi(iotex_modem_get_clock(NULL));
        if(abs((uint32_t)((aux1.date_time_utc + uptime_now - aux1.last_date_time_update)/1000) - modem_timestamp) < 2)
        {
            sys_time_aux.date_time_utc = aux1.date_time_utc;
            sys_time_aux.last_date_time_update = aux1.last_date_time_update;
        }
        else 
        {
            if(abs((uint32_t)((aux1.date_time_utc - aux1.last_date_time_update - aux2.date_time_utc + aux2.last_date_time_update)/1000)) < 2)
            {
                sys_time_aux.date_time_utc = aux1.date_time_utc;
                sys_time_aux.last_date_time_update = aux1.last_date_time_update;
            }
            else
            {
                ret = 1;
                goto ntp_error;
            }
        }
    }
    else {
        ret = 2;
        goto ntp_error;
    }
    return  ret;
ntp_error:
    ntp_err_show(ret);
    while(1)
    {
        k_sleep(K_MSEC(500));
    }
}

int syncNTPTime(void) 
{
    s64_t uptime_now;
    uint32_t last_update_time = 0;

    sys_mutex_lock(&ntp_mutex, K_FOREVER);
    uptime_now = k_uptime_get();
    if((uptime_now - sys_time_aux.last_date_time_update)/1000 >= CONFIG_DATE_TIME_UPDATE_INTERVAL_SECONDS) {
        readNTP();
    }
    sys_mutex_unlock(&ntp_mutex);
    return  0;
}
s64_t getSysTimestamp_ms(void)
{
    s64_t timestamp, uptime_now;

    sys_mutex_lock(&ntp_mutex, K_FOREVER);
    uptime_now = k_uptime_get();
    timestamp = sys_time_aux.date_time_utc + uptime_now - sys_time_aux.last_date_time_update;
    sys_mutex_unlock(&ntp_mutex);
    LOG_DBG("ntp timestamp in ms:%lli \n", timestamp);
    return  timestamp;
}

uint32_t getSysTimestamp_s(void)
{
    uint32_t timestamp;

    timestamp = atoi(iotex_modem_get_clock(NULL));
    LOG_DBG("modem timestamp in second:%d \n", timestamp);
    timestamp = (uint32_t)((getSysTimestamp_ms() + 500)/1000);
    LOG_DBG("ntp timestamp in second:%d \n", timestamp);
    return  timestamp;
}

/*
void printTick(void) {
    static uint32_t ct = 0;
    ct++;
    if(ct >= 11) {
        ct = 0;
        LOG_ERR("----- system tick : %lli -----\n", z_tick_get());
    }
}
*/


