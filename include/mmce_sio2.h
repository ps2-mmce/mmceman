#ifndef MMCE_SIO2_H
#define MMCE_SIO2_H

#include <tamtypes.h>

//El Isra's secrman hook
int mmceman_sio2_hook_secrman();
int mmceman_sio2_unhook_secrman();

//Init global td and try to determine port
void mmceman_sio2_init();

//Assign port and update global td
void mmceman_sio2_set_port(int port);

//Send single packet (1-256 bytes)
int mmceman_sio2_send(u8 in_size, u8 out_size, u8 *in_buf, u8 *out_buf);

//Read raw payload
int mmceman_sio2_read_raw(u32 size, u8 *buffer);

//Write raw payload
int mmceman_sio2_write_raw(u32 size, u8 *buffer);

//Poll card
int mmceman_sio2_wait_equal(u8 value, u32 timeout);

#endif