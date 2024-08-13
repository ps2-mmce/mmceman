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

IRX_ID(MODNAME, MAJOR, MINOR);

//TODO: Temporary until 'mmce1:' and 'mmce2:' are implemented
static int auto_detect_port()
{
    int card_info = -1;

    mmce_sio2_set_port(2);
    for (int i = 0; i < 3; i++) {
        card_info = mmce_cmd_ping();
        if (card_info != -1) {
            DPRINTF("Found card in port 2\n");
            
            if (((card_info & 0xFF00) >> 8) == 1) {
                DPRINTF("Product id: 1 (SD2PSX)\n");
            } else if (((card_info & 0xFF00) >> 8) == 2) {
                DPRINTF("Product id: 2 (MemCard PRO2)\n");
            } else {
                DPRINTF("Product id: %i (unknown)\n", ((card_info & 0xFF00) >> 8));
            }
            
            DPRINTF("Revision id: %i\n", (card_info & 0xFF));
            DPRINTF("Protocol Version: %i\n", (card_info & 0xFF0000) >> 16);

            return 0;
        }
    }

    mmce_sio2_set_port(3);
    for (int i = 0; i < 3; i++) {
        card_info = mmce_cmd_ping();
        if (card_info != -1) {
            DPRINTF("Found card in port 3\n");
            
            if (((card_info & 0xFF00) >> 8) == 1) {
                DPRINTF("Product id: 1 (SD2PSX)\n");
            } else if (((card_info & 0xFF00) >> 8) == 2) {
                DPRINTF("Product id: 2 (MemCard PRO2)\n");
            } else {
                DPRINTF("Product id: %i (unknown)\n", ((card_info & 0xFF00) >> 8));
            }
            
            DPRINTF("Revision id: %i\n", (card_info & 0xFF));
            DPRINTF("Protocol Version: %i\n", (card_info & 0xFF0000) >> 16);

            return 0;
        }
    }

    DPRINTF("Failed to find MMCE in either port\n");
    return -1;
}

int __start(int argc, char *argv[])
{
    int rv;
    
    printf("Multipurpose Memory Card Emulator Manager (MMCEMAN) v%d.%d by bbsan2k, El_isra, and qnox32\n", MAJOR, MINOR);

    //Install hooks
    rv = mmce_sio2_init();
    if (rv != 0) {
        DPRINTF("mmce_sio2_init failed, rv %i\n", rv);
        return MODULE_NO_RESIDENT_END;
    }

    //TEMP: until 'mmce1:' and 'mmce2:' are implemented
    auto_detect_port();

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