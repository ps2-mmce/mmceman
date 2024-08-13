#ifndef MMCEDRV_CONFIG_H
#define MMCEDRV_CONFIG_H

#include <stdint.h>

#define MODULE_SETTINGS_MAGIC 0xf1f2f3f4
#define PATH_MAX_LEN 64

/* The game ISO is to be opened by MMCEMAN prior to 
*  resetting the IOP and loading MMCEDRV */
struct mmcedrv_config
{
    uint32_t magic; //Magic number to find

    uint8_t port;
    uint8_t iso_fd;
    uint8_t vmc_fd;
    //char iso_path[PATH_MAX_LEN];
} __attribute__((packed));


#endif