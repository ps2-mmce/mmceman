#ifndef MODULE_DEBUG_H
#define MODULE_DEBUG_H

#ifdef MMCEDRV
#define MODNAME "mmcedrv"
#elif defined(MMCEMON)
#define MODNAME "mmcmon"
#else
#define MODNAME "mmceman"
#endif
//#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#define DPRINTF(fmt, x...) printf(MODNAME": "fmt, ##x)
#else
#define DPRINTF(x...) 
#endif

#define RPRINTF(fmt, x...) printf(MODNAME": "fmt, ##x) // Resident Printf. same as DPRINTF but not stripped from program at DEBUG not defined

#endif
