#ifndef MODULE_DEBUG_H
#define MODULE_DEBUG_H

#define MODNAME "MMCEMAN"

#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#define DPRINTF(fmt, x...) printf(MODNAME": "fmt, ##x)
#else
#define DPRINTF(x...) 
#endif

#endif
