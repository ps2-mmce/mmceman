/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# taken from MX4SIO driver for simplicity.
# all credits go to maximus32
*/

#include <stdio.h>
#include <string.h>
#include <thsemap.h>
#include <intrman.h>

#include "ioplib.h"
#include "sio2man.h"
#include "sio2man_hook.h"

// #define DEBUG  //comment out this line when not debugging
#include "module_debug.h"

#if 0
// Only needed for mx4sio
#define PORT_NR 3
#endif
#ifndef M_DEBUG
#define M_DEBUG DPRINTF
#endif

// sio2man function typedefs
typedef void (*psio2_transfer_init)(void);
typedef int (*psio2_transfer)(sio2_transfer_data_t *td);
typedef void (*psio2_transfer_reset)(void);

struct sio2man_hook_data
{
    iop_library_t *m_lib;
    psio2_transfer_init m_lock;
#ifdef PORT_NR
    psio2_transfer m_25_psio2_transfer;
#endif
    psio2_transfer_reset m_26_psio2_transfer_reset;
};

static struct sio2man_hook_data g_hook_data[2];
static iop_library_t *g_loadcore_lib;

#ifdef PORT_NR
static sio2_transfer_data_t g_null_td;
// Generic sio2man transfer function
static int _sio2_transfer(psio2_transfer transfer_func, sio2_transfer_data_t *td)
{
    unsigned int i;

    //M_DEBUG("%s\n", __FUNCTION__);

    // Do not allow transfers to/from our used port
    for (i = 0; i < (sizeof(td->regdata)/sizeof(td->regdata[0])); i++) {
        // Last transfer, we're ok.
        if (td->regdata[i] == 0)
            break;

        // Wrong port; behave as a no-op.
        // This will allow the transfer reset behavior for old library to work.
        if ((td->regdata[i] & 3) == PORT_NR)
            return transfer_func(&g_null_td);
    }

    return transfer_func(td);
}

// Hooked sio2man functions
static int _1_25_sio2_transfer(sio2_transfer_data_t *td) { return _sio2_transfer(g_hook_data[0].m_25_psio2_transfer, td); }
static int _2_25_sio2_transfer(sio2_transfer_data_t *td) { return _sio2_transfer(g_hook_data[1].m_25_psio2_transfer, td); }
#endif

static void unhook_loadcore(void);

static void _sio2man_hook(iop_library_t *lib)
{
    int state;
    int major_version;
    unsigned int table_size;

    // Disable interrupts to prevent race conditions with MC/PAD libraries
    CpuSuspendIntr(&state);
    major_version = (lib->version >> 8) & 0xFF;
    table_size = ioplib_getTableSize(lib);
    if (major_version < 1 || major_version > 2 || table_size < 27) {
        M_DEBUG("ERROR: sio2man version 0x%x with %u exports is not supported\n", lib->version, table_size);
        CpuResumeIntr(state);
        return;
    }

    M_DEBUG("Installing sio2man hooks for version 0x%x\n", lib->version);

    if (g_hook_data[major_version - 1].m_lib) {
        M_DEBUG("Warning: trying to hook sio2man version 0x%x\n", lib->version);
        M_DEBUG("         while version 0x%x already hooked\n", g_hook_data[major_version - 1].m_lib->version);
    }
    g_hook_data[major_version - 1].m_lib = lib;
    g_hook_data[major_version - 1].m_lock = table_size >= 51 ? lib->exports[50] : lib->exports[24];
#ifdef PORT_NR
    g_hook_data[major_version - 1].m_25_psio2_transfer = ioplib_hookSameExportEntries(lib, 25, (major_version == 1) ? &_1_25_sio2_transfer : &_2_25_sio2_transfer);
#endif
    g_hook_data[major_version - 1].m_26_psio2_transfer_reset = lib->exports[26];
#ifdef PORT_NR
    ioplib_relinkExports(lib);
#endif
    if (g_hook_data[0].m_lib && g_hook_data[1].m_lib)
        unhook_loadcore();
    CpuResumeIntr(state);
}

// loadcore hook
static int (*pRegisterLibraryEntries)(iop_library_t *lib);
static int hookRegisterLibraryEntries(iop_library_t *lib)
{
    int ret;
    M_DEBUG("RegisterLibraryEntries: %s 0x%x\n", lib->name, lib->version);

    ret = pRegisterLibraryEntries(lib);
    // If library already exists or other error, then do not hook
    if (ret)
        return ret;
    if (!strcmp(lib->name, "sio2man"))
        _sio2man_hook(lib);
    return ret;
}

static void unhook_loadcore(void)
{
    int state;

    CpuSuspendIntr(&state);
    if (g_loadcore_lib) {
        if (pRegisterLibraryEntries)
            ioplib_hookSameExportEntries(g_loadcore_lib, 6, pRegisterLibraryEntries);
        g_loadcore_lib = NULL;
    }
    CpuResumeIntr(state);
}

static int generic_hook_iterate_cb(iop_library_t *lib, void *userdata)
{
    (void)userdata;
    if (userdata && !*(iop_library_t **)userdata)
        *(iop_library_t **)userdata = lib;
    return 1;
}

static int sio2man_hook_iterate_cb(iop_library_t *lib, void *userdata)
{
    (void)userdata;
    switch ((lib->version >> 8) & 0xFF)
    {
        case 1:
        case 2:
        {
            _sio2man_hook(lib);
            return (g_hook_data[0].m_lib && g_hook_data[1].m_lib) ? 1 : 0;
        }
        default:
            return 1;
    }
}

int sio2man_hook_init()
{
    M_DEBUG("%s\n", __FUNCTION__);

    // Hook into 'sio2man' now if it's already loaded
    if (!ioplib_iterateByName("sio2man", &sio2man_hook_iterate_cb, NULL)) {
        // Hook into 'loadcore' so we know when sio2man is loaded in the future
        ioplib_iterateByName("loadcore", &generic_hook_iterate_cb, &g_loadcore_lib);
        if (!g_loadcore_lib)
            return -1;
        pRegisterLibraryEntries = ioplib_hookSameExportEntries(g_loadcore_lib, 6, hookRegisterLibraryEntries);
        ioplib_relinkExports(g_loadcore_lib);
    }

    return 0;
}

void sio2man_hook_deinit()
{
    unsigned int i;

    M_DEBUG("%s\n", __FUNCTION__);

    // Unhook 'sio2man'
    for (i = 0; i < (sizeof(g_hook_data)/sizeof(g_hook_data[0])); i += 1) {
        if (g_hook_data[i].m_lib) {
#ifdef PORT_NR
            if (g_hook_data[i].m_25_psio2_transfer) {
                ioplib_hookSameExportEntries(g_hook_data[i].m_lib, 25, g_hook_data[i].m_25_psio2_transfer);
                g_hook_data[i].m_25_psio2_transfer = NULL;
            }
            ioplib_relinkExports(g_hook_data[i].m_lib);
#endif
            g_hook_data[i].m_lib = NULL;
        }
    }

    unhook_loadcore();
}

void sio2man_hook_sio2_lock()
{
    // Lock sio2man driver so we can use it exclusively
    if (g_hook_data[1].m_lock)
        g_hook_data[1].m_lock();
    else if (g_hook_data[0].m_lock)
        g_hook_data[0].m_lock();
}

void sio2man_hook_sio2_unlock()
{
    // Unlock sio2man driver
    if (g_hook_data[1].m_26_psio2_transfer_reset)
        g_hook_data[1].m_26_psio2_transfer_reset();
    else if (g_hook_data[0].m_26_psio2_transfer_reset)
        g_hook_data[0].m_26_psio2_transfer_reset();
}
