#include <zephyr/device.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include "local_storage.h"

LOG_MODULE_REGISTER(local_storage, CONFIG_ASSET_TRACKER_LOG_LEVEL);

/* Flash partition for NVS */
#define NVS_FLASH_DEVICE FLASH_AREA_DEVICE(storage)
/* Flash block size in bytes */
#define NVS_SECTOR_SIZE  (DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size))
#define NVS_SECTOR_COUNT 2
/* Start address of the filesystem in flash */
#define NVS_STORAGE_OFFSET FLASH_AREA_OFFSET(storage)

static struct nvs_fs __local_storage = {
	.sector_size = NVS_SECTOR_SIZE,
	.sector_count = NVS_SECTOR_COUNT,
	.offset = NVS_STORAGE_OFFSET,
};

/*
** @brief: Initialize local storage(zephyr nvs subsystem
** #CONFIG_NVS_LOCAL_STORAGE_SECTOR_COUNT define how many pages used as local storage
*/
int iotex_local_storage_init(void) {
    int ret;
    struct flash_pages_info info;

	__local_storage.flash_device = NVS_FLASH_DEVICE;
	if (__local_storage.flash_device == NULL) {
		return -ENODEV;
	}

    if ((ret = nvs_mount(&__local_storage))) {
        LOG_ERR("Init local storage failed: %d\n", ret);
        return -1;
    }
    /* Print local storage fress space */
    LOG_INF("Local storage sector count: %u, freespace in local storage: %u, page size: %u\n",
        CONFIG_NVS_LOCAL_STORAGE_SECTOR_COUNT, nvs_calc_free_space(&__local_storage), info.size);

    return  0;
}

/*
** @breif: Clear local storage all data will gone
*/
int iotex_local_storage_clear(void) {
    return nvs_clear(&__local_storage);
}

/*
** @brief: Save data to nvs
** #id: data id in nvs, if nothing first time will auto created
** #data: data to write
** #len: data length in byte
** $return: success return 0, failed return negative code
*/
int iotex_local_storage_del(iotex_storage_id id) {
    return nvs_delete(&__local_storage, id) ? -1 : 0;
}

int iotex_local_storage_save(iotex_storage_id id, const void *data, size_t len) {
    return nvs_write(&__local_storage, id, data, len) == len ? 0 : -1;
}

/* Load always read the latest data, same as iotex_local_storage_hist cnt == 0 */
int iotex_local_storage_load(iotex_storage_id id, void *data, size_t len) {
    return nvs_read(&__local_storage, id, data, len) >= len ? 0 : -1;
}

/* History is LIFO zero always indicate the latest one, counter - 1 indicate the first one  */
int iotex_local_storage_hist(iotex_storage_id id, void *data, size_t len, uint16_t cnt) {
    return nvs_read_hist(&__local_storage, id, data, len, cnt) < 0 ? -1 : 0;
}

/* Read all data to #data point buffer, size indicate buffer size return how many items it read */
int iotex_local_storage_readall(iotex_storage_id id, void *data, size_t size, size_t item_len) {
    void *current = data;
    uint16_t read_cnt = 0;

    /* Read all data to buffer, the latest data comes first */
    for (read_cnt = 0; current - data + item_len <= size; read_cnt++, current += item_len) {
        /* No more data */
        if (iotex_local_storage_hist(id, current, item_len, read_cnt)) {
            break;
        }
    }

    return read_cnt;
}
