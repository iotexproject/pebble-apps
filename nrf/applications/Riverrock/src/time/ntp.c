#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/xen/console.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/mutex.h>
#include <modem/lte_lc.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socketutils.h>


LOG_MODULE_REGISTER(ntp, 0);


#define     SNTP_REQUEST_TIMEOUT      5
#define     NTP_DEFAULT_PORT "123"


struct ntp_servers {
	const char *server_str;
	struct addrinfo *addr;
};
/*
    NTP  SERVERS
*/
struct ntp_servers servers[] = {
    {.server_str = "Time2.Stupi.SE"},
    {.server_str = "time.cloudflare.com"},
    {.server_str = "ntp.aliyun.com"},
    {.server_str = "ntp.uio.no"},
    {.server_str = "time.windows.com"},
    {.server_str = "time.google.com"},
    {.server_str = "edu.ntp.org.cn"},
	{.server_str = "ntp.ntsc.ac.cn"},
	{.server_str = "ntp2.nim.ac.cn"},
	{.server_str = "ntp1.nim.ac.cn"}
};

struct sys_mutex ntp_mutex;

struct time_aux {
	int64_t date_time_utc;
	int64_t last_date_time_update;
}sys_time_aux ={
    0,0
};
static struct sntp_time sntp_time;
extern atomic_val_t send_data_enable;
static int readNTP(void);
uint32_t getSysTimestamp_s(void);

int initNTP(void) {
    sys_mutex_init(&ntp_mutex);
    sys_mutex_lock(&ntp_mutex, K_FOREVER);
    readNTP();
    sys_mutex_unlock(&ntp_mutex);
    return  0;
}


static int sntp_time_request(struct ntp_servers *server, uint32_t timeout,
			     struct sntp_time *time)
{
	int err;
	static struct addrinfo hints;
	struct sntp_ctx sntp_ctx;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;

	if (server->addr == NULL) {
		err = getaddrinfo(server->server_str, NTP_DEFAULT_PORT, &hints,
				&server->addr);
		if (err) {
			LOG_WRN("getaddrinfo, error: %d", err);
			return err;
		}
	} else {
		LOG_DBG("Server address already obtained, skipping DNS lookup");
	}

	err = sntp_init(&sntp_ctx, server->addr->ai_addr,
			server->addr->ai_addrlen);
	if (err) {
		LOG_WRN("sntp_init, error: %d", err);
		goto socket_close;
	}

	err = sntp_query(&sntp_ctx, timeout, time);
	if (err) {
		LOG_WRN("sntp_query, error: %d", err);
	}

socket_close:
	sntp_close(&sntp_ctx);
	return err;
}

static int time_NTP_server_get(int skipped_ntp, struct time_aux *aux)
{
	int err, i; 

	for (i = 0; i < ARRAY_SIZE(servers); i++) {
		err =  sntp_time_request(&servers[i],
			MSEC_PER_SEC * SNTP_REQUEST_TIMEOUT,
			&sntp_time);

		if (err) {
			LOG_DBG("Not getting time from NTP server %s, error %d",
				log_strdup(servers[i].server_str), err);
			LOG_DBG("Trying another address...");
			continue;
		}
		LOG_DBG("Got time response from NTP server: %s",log_strdup(servers[i].server_str));

		aux->date_time_utc = (int64_t)sntp_time.seconds * 1000;
		aux->last_date_time_update = k_uptime_get();

		return  i;
	}

	LOG_WRN("Not getting time from any NTP server");

	return -ENODATA;
}

static int readNTP(void) {
    int64_t uptime_now;
    struct time_aux  aux1, aux2;
    uint32_t modem_timestamp;
    const char *modem_tm_str;
    int ret = 0;

    //  mqtt not closed 
    if (atomic_get(&send_data_enable))
        return 1;

    ret = time_NTP_server_get(-1, &aux1);
    if(ret >= 0) {
        uptime_now = k_uptime_get();
        modem_tm_str = iotex_modem_get_clock(NULL);
        if(modem_tm_str != NULL) {
            modem_timestamp = atoi(modem_tm_str);
            if(abs((uint32_t)((aux1.date_time_utc + uptime_now - aux1.last_date_time_update)/1000) - modem_timestamp) <= 2){
                sys_time_aux.date_time_utc = aux1.date_time_utc;
                sys_time_aux.last_date_time_update = aux1.last_date_time_update;
            }
            else {
                ret = time_NTP_server_get(ret, &aux2);
                if(ret >= 0) {
                    if(abs((uint32_t)((aux1.date_time_utc - aux1.last_date_time_update - aux2.date_time_utc + aux2.last_date_time_update)/1000)) <= 2){
                        sys_time_aux.date_time_utc = aux1.date_time_utc;
                        sys_time_aux.last_date_time_update = aux1.last_date_time_update;
                    }
                    else
                    {
                        ret = 1;
                        goto ntp_error;
                    }
                }
                else {
                    ret = 1;
                    goto ntp_error;
                }
            }
        }
        else {
            ret = 1;
            goto ntp_error;
        }
    }
    else {
        ret = 1;
        goto ntp_error;
    }
    return  ret;

ntp_error:
    ntp_err_show();
    while(1)
    {
        k_sleep(K_SECONDS(100));
    }
}


int syncNTPTime(void)
{
    int64_t uptime_now;
    uint32_t last_update_time = 0;

    sys_mutex_lock(&ntp_mutex, K_FOREVER);
    uptime_now = k_uptime_get();
    if((uptime_now - sys_time_aux.last_date_time_update)/1000 >= 20*60*60) {
        readNTP();
    }
    sys_mutex_unlock(&ntp_mutex);
    return  0;
}

int64_t getSysTimestamp_ms(void)
{
    int64_t timestamp, uptime_now;

    sys_mutex_lock(&ntp_mutex, K_FOREVER);
    uptime_now = k_uptime_get();
    timestamp = sys_time_aux.date_time_utc + uptime_now - sys_time_aux.last_date_time_update;
    sys_mutex_unlock(&ntp_mutex);
    timestamp = (sys_time_aux.date_time_utc ? timestamp : 0);
    LOG_DBG("ntp timestamp in ms:%lli \n", timestamp);
    return  timestamp;
}

uint32_t getSysTimestamp_s(void)
{
    uint32_t timestamp;
    
    /*LOG_DBG("modem time: %d\n",atoi(iotex_modem_get_clock(NULL)));*/
    timestamp = (uint32_t)(getSysTimestamp_ms()/1000);
    /*LOG_DBG("ntp timestamp in second:%d \n", timestamp);*/
    return  timestamp;
}



