/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FlashDB configuration file for ESP32-S3
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* using KVDB feature */
#define FDB_USING_KVDB

#ifdef FDB_USING_KVDB
/* Auto update KV to latest default when current KVDB version number is changed. @see fdb_kvdb.ver_num */
/* #define FDB_KV_AUTO_UPDATE */
#endif

/* using TSDB (Time series database) feature */
#ifndef FDB_USING_TSDB
#define FDB_USING_TSDB
#endif

/* Using FAL storage mode */
#ifndef FDB_USING_FAL_MODE
#define FDB_USING_FAL_MODE
#endif

#ifdef FDB_USING_FAL_MODE
/* the flash write granularity, unit: bit
 * ESP32-S3 flash uses SPI flash which typically has 1-bit granularity */
#define FDB_WRITE_GRAN 1 /* @note you must define it for a value */
#endif

/* Using file storage mode by LIBC file API, like fopen/fread/fwrte/fclose */
/* #define FDB_USING_FILE_LIBC_MODE */

/* Using file storage mode by POSIX file API, like open/read/write/close */
/* #define FDB_USING_FILE_POSIX_MODE */

/* MCU Endian Configuration, default is Little Endian Order. */
/* #define FDB_BIG_ENDIAN */

/* log print macro using ESP_LOG.
 * Need to include esp_log.h when setting FDB_PRINT to ESP_LOGI().
 * default EF_PRINT macro is printf() */
#include <esp_log.h>
#define FDB_PRINT(...)              ESP_LOGI("FlashDB", __VA_ARGS__)

/* print debug information */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */