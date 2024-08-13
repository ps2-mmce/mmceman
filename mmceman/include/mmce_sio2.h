#ifndef MMCE_SIO2_H
#define MMCE_SIO2_H

#include <tamtypes.h>

int mmce_sio2_init();

void mmce_sio2_deinit();

void mmce_sio2_lock();

void mmce_sio2_unlock();

//Assign port and update global td
void mmce_sio2_set_port(int port);

//Get currently assigned port
int mmce_sio2_get_port();

//Send single packet (1-256 bytes)
int mmce_sio2_send(u8 in_size, u8 out_size, u8 *in_buf, u8 *out_buf);

//Read raw payload
int mmce_sio2_read_raw(u32 size, u8 *buffer);

//Write raw payload
int mmce_sio2_write_raw(u32 size, u8 *buffer);

//Poll card
int mmce_sio2_wait_equal(u8 value, u32 timeout);

#endif