#include <stdio.h>
#include <string.h>
#include <sysclib.h>

#include "ioplib.h"
#include "irx_imports.h"

#include "module_debug.h"

#include "mmce_sio2.h"
#include "mmce_cmds.h"
#include "mmce_fs.h"

#define MAJOR 2
#define MINOR 1

int __start(int argc, char *argv[])
{
    int rv;
    
    printf("Multipurpose Memory Card Emulator Manager (MMCEMAN) v%d.%d by El_isra, qnox32 and bbsan2k\n", MAJOR, MINOR);

    //Run El Isra's SECRMAN hook
    rv = mmceman_sio2_hook_secrman();
    if (rv != 0) {
        DPRINTF("SECRMAN hook failed\n");
        return MODULE_NO_RESIDENT_END;
    }

    //TODO: temp
    mmceman_sio2_set_port(3);

    //Init fileio driver
    mmceman_fs_register();

    iop_library_t * lib_modload = ioplib_getByName("modload");
    if (lib_modload != NULL) {
        DPRINTF("modload 0x%x detected\n", lib_modload->version);
        if (lib_modload->version > 0x102) //IOP is running a MODLOAD version wich supports unloading IRX Modules
            return MODULE_REMOVABLE_END; // and we do support getting unloaded...
    } else {
        DPRINTF("modload not detected! this is serious!\n");
    }

    return MODULE_RESIDENT_END;
}

int __stop(int argc, char *argv[])
{
    DPRINTF("Unloading module\n");
    
    //Unhook secrman
    mmceman_sio2_unhook_secrman();

    return MODULE_NO_RESIDENT_END;
}

int _start(int argc, char *argv[])
{
    if (argc >= 0) 
        return __start(argc, argv);
    else
        return __stop(-argc, argv);
}