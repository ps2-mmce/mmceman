#ifndef MMCE_SIO2_H
#define MMCE_SIO2_H

#include <tamtypes.h>
#include <thbase.h>

extern iop_sys_clock_t timeout_200ms;
extern iop_sys_clock_t timeout_500ms;
extern iop_sys_clock_t timeout_2s;

int mmce_sio2_init();

//Remove hooks and deinit
void mmce_sio2_deinit();

//Lock SIO2 for execlusive access
void mmce_sio2_lock();

//Unlock SIO2
void mmce_sio2_unlock();

//Assign port and update global td
void mmce_sio2_set_port(int port);

//Update PCTRL1_INTER_BYTE_PER value
void mmce_sio2_update_ack_wait_cycles(int cycles);

//Get currently assigned port
int mmce_sio2_get_port();

//Transfer single packet (1-256 bytes)
int mmce_sio2_tx_rx_pio(u8 tx_size, u8 rx_size, u8 *tx_buf, u8 *rx_buf, iop_sys_clock_t *timeout);

//Read only chunks of 256
int mmce_sio2_rx_dma(u8 *buffer, u32 size);

//Read as many chunks of 256 as possible with DMA
int mmce_sio2_rx_mixed(u8 *buffer, u32 size);

//Write raw payload
int mmce_sio2_tx_mixed(u8 *buffer, u32 size);

//Poll card
int mmce_sio2_wait_equal(u8 value, u32 timeout);



#endif
