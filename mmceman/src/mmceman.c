#include <intrman.h>
#include <stdio.h>
#include <string.h>
#include <sysclib.h>

#include "ioplib.h"
#include "irx_imports.h"

#include "sio2man_hook.h"
#include "mmce_sio2.h"
#include "mmce_cmds.h"
#include "mmce_fs.h"

#include "module_debug.h"

#define MAJOR 2
#define MINOR 1

IRX_ID("mmceman", MAJOR, MINOR);

static void mmce_init(int port)
{
    int res = -1;

    mmce_sio2_set_port(port);

    for (int i = 0; i < 6; i++) {
        res = mmce_cmd_ping();
        if (res != -1) {
            DPRINTF("Found card in port %i\n", port);

            if (((res & 0xFF00) >> 8) == 1) {
                DPRINTF("Product id: 1 (SD2PSX)\n");
            } else if (((res & 0xFF00) >> 8) == 2) {
                DPRINTF("Product id: 2 (MemCard PRO2)\n");
            } else {
                DPRINTF("Product id: %i (unknown)\n", ((res & 0xFF00) >> 8));
            }

            DPRINTF("Revision id: %i\n", (res & 0xFF));
            DPRINTF("Protocol Version: %i\n", (res & 0xFF0000) >> 16);

            /*
            DPRINTF("Resetting MMCE FS...");

            res = mmce_cmd_fs_reset();
            if (res != 0) 
                DPRINTF("Failed: %i\n", res);
            else
                DPRINTF("Okay\n");*/

            break;
        }
    }
}

int __start(int argc, char *argv[])
{
    int rv;

    printf("Multipurpose Memory Card Emulator Manager (MMCEMAN) v%d.%d by bbsan2k, El_isra, and qnox32\n", MAJOR, MINOR);

    //Install hooks
    rv = mmce_sio2_init();
    if (rv != 0) {
        DPRINTF("%s: mmce_sio2_init failed, rv %i\n", __func__, rv);
        return MODULE_NO_RESIDENT_END;
    }

    //Try to init MMCE's on both ports
    mmce_init(2);
    mmce_init(3);

    //Attach filesystem to iomanX
    mmce_fs_register();

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
