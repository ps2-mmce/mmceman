#ifndef MMCEDRV_CONFIG_H
#define MMCEDRV_CONFIG_H

#include <stdint.h>

#define MODULE_SETTINGS_MAGIC 0xf1f2f3f4
#define PATH_MAX_LEN 64

#define MMCEDRV_TYPE_ISO 0
#define MMCEDRV_TYPE_VMC 1

enum mmcedrv_settings {
    MMCEDRV_SETTING_PORT = 0x0,
    MMCEDRV_SETTING_ISO_FD = 0x1,
    MMCEDRV_SETTING_VMC_FD = 0x2,
};

/* Game and VMC's are to be opened by MMCEMAN
 * prior to resetting the IOP and loading MMCEDRV */
struct mmcedrv_config
{
    uint32_t magic; //Magic number to find

    uint8_t port;
    uint8_t iso_fd;
    uint8_t vmc_fd;
} __attribute__((packed));

#endif