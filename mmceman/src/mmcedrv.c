#include <intrman.h>
#include <stdio.h>
#include <string.h>
#include <sysclib.h>

#include "ioplib.h"
#include "irx_imports.h"

#include "mmce_cmds.h"
#include "mmcedrv_config.h"
#include "mmce_fs.h"
#include "mmce_sio2.h"

#include "module_debug.h"

#define MAJOR 0
#define MINOR 1

IRX_ID(MODNAME, MAJOR, MINOR);

struct mmcedrv_config config = {MODULE_SETTINGS_MAGIC};
extern struct irx_export_table _exp_mmcedrv;

s64 mmcedrv_get_size(int fd)
{
    int res;
    s64 position = 0;

    u8 wrbuf[0xe];
    u8 rdbuf[0xe];

    memset(wrbuf, 0, 0xd);
    
    wrbuf[0x0] = MMCE_ID;               //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_LSEEK64;   //Command
    wrbuf[0x2] = MMCE_RESERVED;         //Reserved
    wrbuf[0x3] = fd;                    //File descriptor
    wrbuf[0xc] = 2;                     //whence, SEEK_END

    mmce_sio2_lock();

    //Packet #1: Command, file descriptor, offset, and whence
    res = mmce_sio2_send(0xe, 0xe, wrbuf, rdbuf);
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

    if (rdbuf[0xd] != 0) {
        DPRINTF("%s ERROR: Returned %i, Expected 0\n", __func__, rdbuf[0xd]);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2 - n: Polling ready
    res = mmce_sio2_wait_equal(1, 128000);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Polling timedout\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: Position
    res = mmce_sio2_send(0x0, 0xA, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    mmce_sio2_unlock();

    position  = (s64)rdbuf[0x1] << 56;
    position |= (s64)rdbuf[0x2] << 48;
    position |= (s64)rdbuf[0x3] << 40;
    position |= (s64)rdbuf[0x4] << 32;
    position |= (s64)rdbuf[0x5] << 24;
    position |= (s64)rdbuf[0x6] << 16;
    position |= (s64)rdbuf[0x7] << 8;
    position |= (s64)rdbuf[0x8];

    DPRINTF("%s position %llu\n", __func__, position);

    return position;
}

int mmcedrv_read_sector(int fd, u32 sector, u32 count, void *buffer)
{
    int res;
    int sectors_read = 0;

    u8 wrbuf[0xB];
    u8 rdbuf[0xB];

    DPRINTF("%s fd: %i, starting sector: %i, count: %i\n", __func__, fd, sector, count);

    wrbuf[0x0] = MMCE_ID;                 //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_READ_SECTOR; //Command
    wrbuf[0x2] = MMCE_RESERVED;           //Reserved
    wrbuf[0x3] = fd;                      //File Descriptor    

    wrbuf[0x4] = (sector & 0x00FF0000) >> 16;
    wrbuf[0x5] = (sector & 0x0000FF00) >> 8;
    wrbuf[0x6] = (sector & 0x000000FF);

    wrbuf[0x7] = (count & 0x00FF0000) >> 16;
    wrbuf[0x8] = (count & 0x0000FF00) >> 8;
    wrbuf[0x9] = (count & 0x000000FF);

    wrbuf[0xA] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command, file descriptor, and size
    res = mmce_sio2_send(0xB, 0xB, wrbuf, rdbuf);
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

    //Packet #2 - n: Raw read data
    res = mmce_sio2_read_raw((count * 2048), buffer);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: Sectors read
    res = mmce_sio2_send(0x0, 0x5, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        //printf("timeout\n");
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


//Called through CDVDMAN MMCE Device
void mmcedrv_set_port(int port)
{
    mmce_sio2_set_port(port);
}

int __start(int argc, char *argv[])
{
    int rv;

    printf("Multipurpose Memory Card Emulator Driver (MMCEDRV) v%d.%d\n", MAJOR, MINOR);

    DPRINTF("Starting MMCEDRV with:\n");
    DPRINTF("Port: %i\n", config.port);
    DPRINTF("ISO fd: %i\n", config.iso_fd);
    DPRINTF("VMC fd: %i\n", config.vmc_fd);

    //Install hooks
    rv = mmce_sio2_init();
    if (rv != 0) {
        DPRINTF("mmce_sio2_init failed, rv %i\n", rv);
        return MODULE_NO_RESIDENT_END;
    }

    mmce_sio2_set_port(3);

    //Set port
    //mmce_sio2_set_port(config.port);

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
