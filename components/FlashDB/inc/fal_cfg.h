#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <stdint.h>

/* Enable debug output for FAL */
#define FAL_DEBUG 1

/* Enable partition table configuration in fal_cfg.h */
#define FAL_PART_HAS_TABLE_CFG

/* ===================== Flash device Configuration ========================= */
extern const struct fal_flash_dev esp32_flash;

/* flash device table */
#define FAL_FLASH_DEV_TABLE \
{                           \
    &esp32_flash,           \
}

/* ====================== Partition Configuration ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG
/* partition table */
#define FAL_PART_TABLE                                                              \
{                                                                                   \
    {FAL_PART_MAGIC_WORD, "flashdb", "esp32_flash", 0, 1024*1024, 0}, \
}
#endif /* FAL_PART_HAS_TABLE_CFG */

#endif /* _FAL_CFG_H_ */