/*
 * Copyright (c) 2024, Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include <esp_partition.h>

#include <string.h>
#include <fal.h>

#define FLASH_ERASE_MIN_SIZE (4 * 1024)

#define LOCKER_ENABLE
#ifdef LOCKER_ENABLE
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock = NULL;
#endif

const static esp_partition_t *partition;

#ifdef LOCKER_ENABLE
#define LOCK()                                 \
    do                                         \
    {                                          \
        xSemaphoreTake(s_lock, portMAX_DELAY); \
    } while (0)

#define UNLOCK()                \
    do                          \
    {                           \
        xSemaphoreGive(s_lock); \
    } while (0)
#else
#define LOCK()
#define UNLOCK()
#endif

static int init(void)
{
    ESP_LOGD("FAL", "FAL init called");
#ifdef LOCKER_ENABLE
    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateCounting(1, 1);
        assert(s_lock != NULL);
    }
#endif

    // Find the partition named "flashdb" as defined in partitions.csv
    partition = esp_partition_find_first(0x40, 0x00, "flashdb");

    if (partition == NULL)
    {
        ESP_LOGE("FAL", "FlashDB partition not found!");
        return -1;
    }

    ESP_LOGI("FAL", "FlashDB partition found at 0x%08x, size: %d bytes",
             partition->address, partition->size);

    return 0;
}

static int read(long offset, uint8_t *buf, size_t size)
{
    esp_err_t ret;
    ESP_LOGD("FAL", "FAL read: offset=0x%lx, size=%u", offset, size);

    if (partition == NULL)
    {
        return -1;
    }

    LOCK();
    ret = esp_partition_read(partition, offset, buf, size);
    UNLOCK();

    if (ret != ESP_OK)
    {
        ESP_LOGE("FAL", "FAL read failed: %s", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? size : -1;
}

static int write(long offset, const uint8_t *buf, size_t size)
{
    esp_err_t ret;
    ESP_LOGD("FAL", "FAL write: offset=0x%lx, size=%u", offset, size);

    if (partition == NULL)
    {
        return -1;
    }

    LOCK();
    ret = esp_partition_write(partition, offset, buf, size);
    UNLOCK();

    if (ret != ESP_OK)
    {
        ESP_LOGE("FAL", "FAL write failed: %s", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? size : -1;
}

static int erase(long offset, size_t size)
{
    esp_err_t ret;
    size_t aligned_size;
    ESP_LOGD("FAL", "FAL erase: offset=0x%lx, size=%u", offset, size);

    if (partition == NULL)
    {
        return -1;
    }

    // Align size to erase block size (4KB for ESP32)
    aligned_size = ((size + FLASH_ERASE_MIN_SIZE - 1) / FLASH_ERASE_MIN_SIZE) * FLASH_ERASE_MIN_SIZE;

    LOCK();
    ret = esp_partition_erase_range(partition, offset, aligned_size);
    UNLOCK();

    if (ret != ESP_OK)
    {
        ESP_LOGE("FAL", "FAL erase failed: %s", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? aligned_size : -1;
}

/* The flash device definition that will be registered to FAL */
const struct fal_flash_dev esp32_flash =
    {
        .name = "esp32_flash",
        .addr = 0x0,                      // address is relative to beginning of partition
        .len = 1024 * 1024,               // 1MB size of the partition as specified in partitions.csv
        .blk_size = FLASH_ERASE_MIN_SIZE, // 4KB block size for ESP32
        .ops = {init, read, write, erase},
        .write_gran = 1, // 1 bit write granularity for SPI flash
};