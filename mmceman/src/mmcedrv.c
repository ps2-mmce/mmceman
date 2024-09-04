#include <intrman.h>
#include <stdio.h>
#include <string.h>
#include <sysclib.h>

#include "ioplib.h"
#include "irx_imports.h"

#include "mmce_cmds.h"
#include "mmce_fs.h"
#include "mmce_sio2.h"
#include "mmcedrv_config.h"

#include "module_debug.h"

#define MAJOR 0
#define MINOR 1

IRX_ID("mmcedrv", MAJOR, MINOR);

struct mmcedrv_config config = {MODULE_SETTINGS_MAGIC}; //For Neutrino
extern struct irx_export_table _exp_mmcedrv;

static int mmcedrv_iso_fd;
static int mmcedrv_vmc_fd;

s64 mmcedrv_get_size()
{
    int res;
    s64 position = -1;

    u8 wrbuf[0xd];
    u8 rdbuf[0x16];

    wrbuf[0x0] = MMCE_ID;                       //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_LSEEK64;           //Command
    wrbuf[0x2] = MMCE_RESERVED;                 //Reserved
    wrbuf[0x3] = mmcedrv_iso_fd;                //ISO file descriptor

    wrbuf[0x4] = 0;   //Offset
    wrbuf[0x5] = 0;
    wrbuf[0x6] = 0;
    wrbuf[0x7] = 0;
    wrbuf[0x8] = 0;
    wrbuf[0x9] = 0;
    wrbuf[0xa] = 0;
    wrbuf[0xb] = 0;

    wrbuf[0xc] = 2; //Whence SEEK_END

    //Packet #1: Command, file descriptor, offset, and whence
    mmce_sio2_lock();
    res = mmce_sio2_tx_rx_pio(0xd, 0x16, wrbuf, rdbuf, &timeout_500ms);
    mmce_sio2_unlock();
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        return -1;
    }

    position  = (s64)rdbuf[0xd] << 56;
    position |= (s64)rdbuf[0xe] << 48;
    position |= (s64)rdbuf[0xf] << 40;
    position |= (s64)rdbuf[0x10] << 32;
    position |= (s64)rdbuf[0x11] << 24;
    position |= (s64)rdbuf[0x12] << 16;
    position |= (s64)rdbuf[0x13] << 8;
    position |= (s64)rdbuf[0x14];

    DPRINTF("%s position %lli\n", __func__, position);

    return position;
}

int mmcedrv_read_sector(int type, u32 sector, u32 count, void *buffer)
{
    int res;
    int sectors_read = 0;

    u8 wrbuf[0xB];
    u8 rdbuf[0xB];

    DPRINTF("%s type: %i, starting sector: %i, count: %i\n", __func__, type, sector, count);

    wrbuf[0x0] = MMCE_ID;                 //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_READ_SECTOR; //Command
    wrbuf[0x2] = MMCE_RESERVED;           //Reserved

    if (type == 0)
        wrbuf[0x3] = mmcedrv_iso_fd;    //ISO file Descriptor    
    else
        wrbuf[0x3] = mmcedrv_vmc_fd;    //VMC file Descriptor    

    wrbuf[0x4] = (sector & 0x00FF0000) >> 16;
    wrbuf[0x5] = (sector & 0x0000FF00) >> 8;
    wrbuf[0x6] = (sector & 0x000000FF);

    wrbuf[0x7] = (count & 0x00FF0000) >> 16;
    wrbuf[0x8] = (count & 0x0000FF00) >> 8;
    wrbuf[0x9] = (count & 0x000000FF);

    wrbuf[0xA] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command, file descriptor, and size
    res = mmce_sio2_tx_rx_pio(0xB, 0xB, wrbuf, rdbuf, &timeout_500ms);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x9] != 0x0) {
        DPRINTF("%s ERROR: P1 - Got bad return value from card, res %i\n", __func__, rdbuf[0x9]);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2 - n: Read data
    res = mmce_sio2_rx_dma(buffer, (count * 2048));
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: Sectors read
    res = mmce_sio2_tx_rx_pio(0x0, 0x5, wrbuf, rdbuf, &timeout_500ms);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    mmce_sio2_unlock();

    sectors_read  = rdbuf[0x1] << 16;
    sectors_read |= rdbuf[0x2] << 8;
    sectors_read |= rdbuf[0x3];

    if (sectors_read != count) {
        DPRINTF("%s ERROR: bytes read: %i, expected: %i\n", __func__, sectors_read, count);
    }

    return sectors_read;
}

//For OPL, called through CDVDMAN Device
void mmcedrv_config_set(int setting, int value)
{
    switch (setting) {
        case MMCEDRV_SETTING_PORT:
            if (value == 2 || value == 3)
                mmce_sio2_set_port(value);
            else
                DPRINTF("Invalid port setting: %i\n", value);
        break;
        
        case MMCEDRV_SETTING_ISO_FD:
            if (value < 8)
                mmcedrv_iso_fd = value;
        break;
        
        case MMCEDRV_SETTING_VMC_FD:
            if (value < 8)
                mmcedrv_vmc_fd = value;
        break;
        
        default:
        break;
    }
}

int __start(int argc, char *argv[])
{
    int rv;

    printf("Multipurpose Memory Card Emulator Driver (MMCEDRV) v%d.%d\n", MAJOR, MINOR);

    //Install hooks
    rv = mmce_sio2_init();
    if (rv != 0) {
        DPRINTF("mmce_sio2_init failed, rv %i\n", rv);
        return MODULE_NO_RESIDENT_END;
    }

    //For Neutrino only, OPL uses config_set export atm
    DPRINTF("Started with:\n");
    DPRINTF("Port: %i\n", config.port);
    DPRINTF("ISO fd: %i\n", config.iso_fd);
    DPRINTF("VMC fd: %i\n", config.vmc_fd);

    mmce_sio2_set_port(config.port);
    mmcedrv_iso_fd = config.iso_fd;
    mmcedrv_vmc_fd = config.vmc_fd;

    //Register exports
    if (RegisterLibraryEntries(&_exp_mmcedrv) != 0) {
        DPRINTF("ERROR: library already registered\n");
        return MODULE_NO_RESIDENT_END;
    }

    iop_library_t * lib_modload = ioplib_getByName("modload");
    if (lib_modload != NULL) {
        DPRINTF("modload 0x%x detected\n", lib_modload->version);
        if (lib_modload->version > 0x102) //IOP is running a MODLOAD version which supports unloading IRX Modules
            return MODULE_REMOVABLE_END; // and we do support getting unloaded...
    } else {
        DPRINTF("modload not detected! this is serious!\n");
    }

    return MODULE_RESIDENT_END;
}

int __stop(int argc, char *argv[])
{
    DPRINTF("Unloading module\n");
    
    mmce_sio2_deinit();

    return MODULE_NO_RESIDENT_END;
}

int _start(int argc, char *argv[])
{
    if (argc >= 0) 
        return __start(argc, argv);
    else
        return __stop(-argc, argv);
}
