
#include <dmacman.h>
#include <sio2regs.h>
#include <thbase.h>
#include <thevent.h>
#include <intrman.h>


#include <stdio.h>

#include "ioplib.h"
#include "irx_imports.h"
#include "iop_regs.h"

#include "mmce_sio2.h"
#include "sio2man_hook.h"

#include "module_debug.h"

#define EF_SIO2_INTR_COMPLETE	    0x00000200
#define EF_SIO2_TRANSFER_TIMEOUT    0x00000400

static int mmce_port;
static int mmce_slot;

//Port ctrl settings used for all transfers
static u32 mmce_sio2_port_ctrl1[4];
static u32 mmce_sio2_port_ctrl2[4];

static u32 sio2_save_ctrl;
static int event_flag = -1;

//SIO2MAN's intr handler
int (*sio2man_intr_handler_ptr)(void *arg);
void *sio2man_intr_arg_ptr;

//Replacement intr handler
int (*mmce_sio2_intr_handler_ptr)(void *arg) = NULL;
void *mmce_sio2_intr_arg_ptr = NULL;

iop_sys_clock_t timeout_200ms;
iop_sys_clock_t timeout_500ms;
iop_sys_clock_t timeout_2s;

int mmce_sio2_intr_handler(void *arg)
{
	int ef = *(int *)arg;

	inl_sio2_stat_set(inl_sio2_stat_get());
    
	iSetEventFlag(ef, EF_SIO2_INTR_COMPLETE);

	return 1;
}

unsigned int mmce_sio2_timeout_handler(void *arg)
{
    int ef = *(int *)arg;
    iSetEventFlag(ef, EF_SIO2_TRANSFER_TIMEOUT);
    return 0;
}

int mmce_sio2_init()
{
    int rv;

    //Install necessary hooks
    rv = sio2man_hook_init();
    if (rv == -1) {
        DPRINTF("Failed to init sio2man hook\n");
        return -1;
    }

    //Create event flag for intr
	iop_event_t event;

	event.attr = 2;
	event.option = 0;
	event.bits = 0;

	event_flag = CreateEventFlag(&event);
    if (event_flag < 0) {
        DPRINTF("Failed to create event flag\n");
        sio2man_hook_deinit();
        return -2;
    }

    //Assign intr pointers
    mmce_sio2_intr_handler_ptr = &mmce_sio2_intr_handler;
    mmce_sio2_intr_arg_ptr = &event_flag;

    //Ensure IOP DMA channels are enabled
    sceSetDMAPriority(IOP_DMAC_SIO2in, 3);
    sceSetDMAPriority(IOP_DMAC_SIO2out, 3);
    sceEnableDMAChannel(IOP_DMAC_SIO2in);
    sceEnableDMAChannel(IOP_DMAC_SIO2out);

    //200ms
    USec2SysClock(200000, &timeout_200ms);

    //500ms
    USec2SysClock(500000, &timeout_500ms);

    //2s
    USec2SysClock(2000000, &timeout_2s);

    return 0;
}

void mmce_sio2_deinit()
{
    sio2man_hook_deinit();
    DeleteEventFlag(event_flag);
}

void mmce_sio2_set_port(int port)
{
    mmce_port = port;

    for (u8 i = 0; i < 4; i++) {
        mmce_sio2_port_ctrl1[i] = 0;
        mmce_sio2_port_ctrl2[i] = 0;
    }
    
    mmce_sio2_port_ctrl1[port] =
        PCTRL0_ATT_LOW_PER(0x5)      |
        PCTRL0_ATT_MIN_HIGH_PER(0x0) |
        PCTRL0_BAUD0_DIV(0x2)        |
        PCTRL0_BAUD1_DIV(0xff);

    mmce_sio2_port_ctrl2[port] =
        PCTRL1_ACK_TIMEOUT_PER(0xffff)|
        PCTRL1_INTER_BYTE_PER(0x5)    |
        PCTRL1_UNK24(0x0)             |
        PCTRL1_IF_MODE_SPI_DIFF(0x0);
}

void mmce_sio2_update_ack_wait_cycles(int cycles)
{
    DPRINTF("mmceman: setting cycles to: 0x%x\n", cycles);

    mmce_sio2_port_ctrl2[mmce_port] =
        PCTRL1_ACK_TIMEOUT_PER(0xffff)|
        PCTRL1_INTER_BYTE_PER(cycles) |
        PCTRL1_UNK24(0x0)             |
        PCTRL1_IF_MODE_SPI_DIFF(0x0);
}

int mmce_sio2_get_port()
{
    return mmce_port;
}

static inline void mmce_sio2_reg_set_pctrl()
{
    for (u8 i = 0; i < 4; i++) {
        inl_sio2_portN_ctrl1_set(i, mmce_sio2_port_ctrl1[i]);
        inl_sio2_portN_ctrl2_set(i, mmce_sio2_port_ctrl2[i]);
    }
}

void mmce_sio2_lock()
{
    int state;
    int res;

    //Lock sio2man driver so we can use it exclusively
    sio2man_hook_sio2_lock();

    sio2_save_ctrl = inl_sio2_ctrl_get();

    //Swap SIO2MAN's intr handler with ours
    CpuSuspendIntr(&state);

    DisableIntr(IOP_IRQ_DMA_SIO2_OUT, &res);
    DisableIntr(IOP_IRQ_DMA_SIO2_IN, &res);

    DisableIntr(IOP_IRQ_SIO2, &res);
    ReleaseIntrHandler(IOP_IRQ_SIO2);

    RegisterIntrHandler(IOP_IRQ_SIO2, 1, mmce_sio2_intr_handler, &event_flag);
    EnableIntr(IOP_IRQ_SIO2);

    CpuResumeIntr(state);

    //Copy port ctrl settings to SIO2 registers
    mmce_sio2_reg_set_pctrl();
}

void mmce_sio2_unlock()
{
    int state;
    int res;

    CpuSuspendIntr(&state);
    
    DisableIntr(IOP_IRQ_SIO2, &res);
    ReleaseIntrHandler(IOP_IRQ_SIO2);

    if (sio2man_intr_handler_ptr != NULL) {
        RegisterIntrHandler(IOP_IRQ_SIO2, 1, sio2man_intr_handler_ptr, sio2man_intr_arg_ptr);
        EnableIntr(IOP_IRQ_SIO2);
    }

    DisableIntr(IOP_IRQ_DMA_SIO2_OUT, &res);
    DisableIntr(IOP_IRQ_DMA_SIO2_IN, &res);

    CpuResumeIntr(state);

    //Restore ctrl state, and reset STATE + FIFOS
    inl_sio2_ctrl_set(sio2_save_ctrl | 0xc);

    //Unlock sio2man driver
    sio2man_hook_sio2_unlock();
}

int mmce_sio2_tx_rx_pio(u8 tx_size, u8 rx_size, u8 *tx_buf, u8 *rx_buf, iop_sys_clock_t *timeout)
{
    u32 resbits;

    //Reset SIO2 + FIFO pointers, enable interrupts
    inl_sio2_ctrl_set(0x3ac);

    //Add transfer to queue
    inl_sio2_regN_set(0,
                        TR_CTRL_PORT_NR(mmce_port)  |
                        TR_CTRL_PAUSE(0)            |
                        TR_CTRL_TX_MODE_PIO_DMA(0)  |
                        TR_CTRL_RX_MODE_PIO_DMA(0)  |
                        TR_CTRL_NORMAL_TR(1)        |
                        TR_CTRL_SPECIAL_TR(0)       |
                        TR_CTRL_BAUD_DIV(0)         |
                        TR_CTRL_WAIT_ACK_FOREVER(0) |
                        TR_CTRL_TX_DATA_SZ(tx_size)|
                        TR_CTRL_RX_DATA_SZ(rx_size));
    inl_sio2_regN_set(1, 0);

    //Copy data to TX FIFO
    for (int i = 0; i < tx_size; i++) {
        inl_sio2_data_out(tx_buf[i]);
    }

    //Start transfer
    inl_sio2_ctrl_set(inl_sio2_ctrl_get() | 1);

    //Set timeout alarm
    SetAlarm(timeout, mmce_sio2_timeout_handler, &event_flag);

    //Wait for completion or timeout
    WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);
    if (resbits & EF_SIO2_TRANSFER_TIMEOUT) {
        DPRINTF("Transfer timed out, resetting SIO2\n");
        inl_sio2_ctrl_set(0x3ac);   //Reset SIO2
        ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
        return -1;
    }

    //Cancel timeout alarm
    CancelAlarm(mmce_sio2_timeout_handler,  &event_flag);
    
    //Clear flags
    ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));

    //Copy data out of RX FIFO
    for (int i = 0; i < rx_size; i++) {
        rx_buf[i] = inl_sio2_data_in();
    }

    //Check timeout bit
    if ((inl_sio2_stat6c_get() & 0x8000) != 0) {
        return -1;
    }

    return 0;
}

//Simplified read for sectors / MMCEDRV
int mmce_sio2_rx_dma(u8 *buffer, u32 size)
{
    u32 resbits;

    //dma element count (round down)
    u32 elements = size / 256;

    u32 elements_do = 0;
    u32 elements_done = 0;

    //used for all elements 256 bytes in size
    u32 dma_element = TR_CTRL_PORT_NR(mmce_port)      |
                      TR_CTRL_PAUSE(0)                |
                      TR_CTRL_TX_MODE_PIO_DMA(0)      |
                      TR_CTRL_RX_MODE_PIO_DMA(1)      |
                      TR_CTRL_NORMAL_TR(1)            |
                      TR_CTRL_SPECIAL_TR(0)           |
                      TR_CTRL_BAUD_DIV(0)             |
                      TR_CTRL_WAIT_ACK_FOREVER(0)     |
                      TR_CTRL_TX_DATA_SZ(0)           |
                      TR_CTRL_RX_DATA_SZ(256);

   while (elements != 0) {
        //Reset SIO2 + FIFO pointers, enable interrupts
        inl_sio2_ctrl_set(0x3ac);

        elements_do = elements > 16 ? 16 : elements;

        //Copy DMA transfer elements to SIO2 registers
        for (int i = 0; i < elements_do; i++) {
            inl_sio2_regN_set(i, dma_element);
        }

        //Start DMA transfer
        dmac_request(IOP_DMAC_SIO2out, &buffer[elements_done * 256], 0x100 >> 2, elements_do, DMAC_TO_MEM);
        dmac_transfer(IOP_DMAC_SIO2out);

        //Start the transfer
        inl_sio2_ctrl_set(inl_sio2_ctrl_get() | 1);

        //Set timeout alarm
        SetAlarm(&timeout_2s, mmce_sio2_timeout_handler, &event_flag);

        //Wait for completion or timeout
        WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);
        if (resbits & EF_SIO2_TRANSFER_TIMEOUT) {
            DPRINTF("Transfer timed out, resetting SIO2\n");
            inl_sio2_ctrl_set(0x3ac); //Reset SIO2
            ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
            return -1;
        }
        
        //Cancel timeout alarm
        CancelAlarm(mmce_sio2_timeout_handler,  &event_flag);
        
        //Clear flags
        ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));

        elements -= elements_do;
        elements_done += elements_do;
    }

    return 0;
}

//Mixed DMA / PIO RX transfer
int mmce_sio2_rx_mixed(u8 *buffer, u32 size)
{
    u32 resbits;

    //dma element count (round down)
    u32 elements = size / 256;

    //remainder < 256
    u32 pio_size = size - (elements * 256);

    u32 bytes_done = 0;
    u32 elements_do = 0;

    u32 offset = 0;
    u32 offset_pio = 0;

    //used for all elements 256 bytes in size
    u32 dma_element = TR_CTRL_PORT_NR(mmce_port)      |
                      TR_CTRL_PAUSE(0)                |
                      TR_CTRL_TX_MODE_PIO_DMA(0)      |
                      TR_CTRL_RX_MODE_PIO_DMA(1)      |
                      TR_CTRL_NORMAL_TR(1)            |
                      TR_CTRL_SPECIAL_TR(0)           |
                      TR_CTRL_BAUD_DIV(0)             |
                      TR_CTRL_WAIT_ACK_FOREVER(0)     |
                      TR_CTRL_TX_DATA_SZ(0)           |
                      TR_CTRL_RX_DATA_SZ(256);

    //used for all elements less than 256 bytes in size
    u32 pio_element = TR_CTRL_PORT_NR(mmce_port)      |
                      TR_CTRL_PAUSE(0)                |
                      TR_CTRL_TX_MODE_PIO_DMA(0)      |
                      TR_CTRL_RX_MODE_PIO_DMA(0)      |
                      TR_CTRL_NORMAL_TR(1)            |
                      TR_CTRL_SPECIAL_TR(0)           |
                      TR_CTRL_BAUD_DIV(0)             |
                      TR_CTRL_WAIT_ACK_FOREVER(0)     |
                      TR_CTRL_TX_DATA_SZ(0)           |
                      TR_CTRL_RX_DATA_SZ(pio_size);

    while (bytes_done < size) {
        //Reset SIO2 + FIFO pointers, enable interrupts
        inl_sio2_ctrl_set(0x3ac);

        //Used for DMA
        offset = bytes_done;

        elements_do = elements > 16 ? 16 : elements;

        //Copy DMA transfer elements to SIO2 registers
        for (int i = 0; i < elements_do; i++) {
            inl_sio2_regN_set(i, dma_element);
        }

        bytes_done += elements_do * 256;

        //Copy PIO element / term transfer queue if needed
        if (elements_do < 16) {
            if (pio_size != 0x0) {
                inl_sio2_regN_set(elements_do, pio_element);

                if (elements_do + 1 < 16) {
                    inl_sio2_regN_set(elements_do + 1, 0x0);
                }
                offset_pio = bytes_done;
                bytes_done += pio_size;
            } else {
                inl_sio2_regN_set(elements_do, 0x0); //Term transfer queue
            }
        }

        //Start DMA transfer if needed
        if (elements_do != 0) {
            dmac_request(IOP_DMAC_SIO2out, &buffer[offset], 0x100 >> 2, elements_do, DMAC_TO_MEM);
            dmac_transfer(IOP_DMAC_SIO2out);
        }

        //Start the transfer
        inl_sio2_ctrl_set(inl_sio2_ctrl_get() | 1);

        //Set timeout alarm
        SetAlarm(&timeout_2s, mmce_sio2_timeout_handler, &event_flag);

        //Wait for completion or timeout
        WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);
        if (resbits & EF_SIO2_TRANSFER_TIMEOUT) {
            DPRINTF("Detected transfer timeout, attempting to reset SIO2\n");
            inl_sio2_ctrl_set(0x3ac); //Reset SIO2
            ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
            return -1;
        }

        //Cancel timeout alarm
        CancelAlarm(mmce_sio2_timeout_handler,  &event_flag);

        //Clear flags
        ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));

        elements -= elements_do;
    }

    //Copy data from RX FIFO
    if (pio_size != 0x0) {
        for (int i = 0; i < pio_size; i++) {
            buffer[offset_pio + i] = inl_sio2_data_in();
        }
        pio_size = 0;
    }

    return 0;
}

int mmce_sio2_tx_mixed(u8 *buffer, u32 size)
{
    int res;
    u32 resbits;

    //dma element count (round down)
    u32 elements = size / 256;

    //remainder < 256
    u32 pio_size = size - (elements * 256);

    u32 bytes_done = 0;
    u32 elements_do = 0;

    u8 polling = 1;
    u8 rx_buf[3];

    //used for all elements 256 bytes in size
    u32 dma_element = TR_CTRL_PORT_NR(mmce_port)      |
                      TR_CTRL_PAUSE(0)                |
                      TR_CTRL_TX_MODE_PIO_DMA(1)      |
                      TR_CTRL_RX_MODE_PIO_DMA(0)      |
                      TR_CTRL_NORMAL_TR(1)            |
                      TR_CTRL_SPECIAL_TR(0)           |
                      TR_CTRL_BAUD_DIV(0)             |
                      TR_CTRL_WAIT_ACK_FOREVER(0)     |
                      TR_CTRL_TX_DATA_SZ(256)         |
                      TR_CTRL_RX_DATA_SZ(0);

    //used to all elements less than 256 bytes in size
    u32 pio_element = TR_CTRL_PORT_NR(mmce_port)      |
                      TR_CTRL_PAUSE(0)                |
                      TR_CTRL_TX_MODE_PIO_DMA(0)      |
                      TR_CTRL_RX_MODE_PIO_DMA(0)      |
                      TR_CTRL_NORMAL_TR(1)            |
                      TR_CTRL_SPECIAL_TR(0)           |
                      TR_CTRL_BAUD_DIV(0)             |
                      TR_CTRL_WAIT_ACK_FOREVER(0)     |
                      TR_CTRL_TX_DATA_SZ(pio_size)    |
                      TR_CTRL_RX_DATA_SZ(0);

    while(1) {
        //Polling stage
        if (polling == 1) {
            res = mmce_sio2_tx_rx_pio(0, 2, NULL, rx_buf, &timeout_2s);
            if (res == -1) {
                DPRINTF("%s ERROR: Timed out waiting for ready\n", __func__);
                return -1;
            }

            if (bytes_done >= size)
                break;

            //Move to transfer stage
            polling = 0;

        //Transfer stage
        } else if (polling == 0) {
            elements_do = elements > 16 ? 16 : elements;

            //Full 4KB DMA transfer
            if (elements_do == 16) {
                //Reset SIO2 + FIFO pointers, enable interrupts
                inl_sio2_ctrl_set(0x3ac);

                for (int i = 0; i < elements_do; i++) {
                    inl_sio2_regN_set(i, dma_element);
                }

                dmac_request(IOP_DMAC_SIO2in, &buffer[bytes_done], 0x100 >> 2, elements_do, DMAC_FROM_MEM);
                dmac_transfer(IOP_DMAC_SIO2in);

                bytes_done += elements_do * 256;
                elements -= elements_do;

                //Start the transfer
                inl_sio2_ctrl_set(inl_sio2_ctrl_get() | 1);

                //Set timeout alarm
                SetAlarm(&timeout_2s, mmce_sio2_timeout_handler, &event_flag);

                //Wait for completion or timeout
                WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);
                if (resbits & EF_SIO2_TRANSFER_TIMEOUT) {
                    DPRINTF("Detected transfer timeout, attempting to reset SIO2\n");
                    inl_sio2_ctrl_set(0x3ac); //Reset SIO2
                    ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
                    return -1;
                }

                //Cancel timeout alarm
                CancelAlarm(mmce_sio2_timeout_handler,  &event_flag);

                //Clear flags
                ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));

                polling = 1;

            //Sub 4KB DMA transfer
            } else {
                //Remaining DMA elements
                if (elements_do != 0) {

                    //Reset SIO2 + FIFO pointers, enable interrupts
                    inl_sio2_ctrl_set(0x3ac);

                    for (int i = 0; i < elements_do; i++) {
                        inl_sio2_regN_set(i, dma_element);
                    }

                    inl_sio2_regN_set(elements_do, 0);

                    dmac_request(IOP_DMAC_SIO2in, &buffer[bytes_done], 0x100 >> 2, elements_do, DMAC_FROM_MEM);
                    dmac_transfer(IOP_DMAC_SIO2in);

                    bytes_done += elements_do * 256;
                    elements -= elements_do;

                    //Start the transfer
                    inl_sio2_ctrl_set(inl_sio2_ctrl_get() | 1);

                    //Set timeout alarm
                    SetAlarm(&timeout_2s, mmce_sio2_timeout_handler, &event_flag);

                    //Wait for completion or timeout
                    WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);
                    if (resbits & EF_SIO2_TRANSFER_TIMEOUT) {
                        DPRINTF("Detected transfer timeout, attempting to reset SIO2\n");
                        inl_sio2_ctrl_set(0x3ac); //Reset SIO2
                        ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
                        return -1;
                    }

                    //Cancel timeout alarm
                    CancelAlarm(mmce_sio2_timeout_handler,  &event_flag);

                    //Clear flags
                    ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
                }
                //PIO element
                if (pio_size != 0) {
                    
                    //Reset SIO2 + FIFO pointers, enable interrupts
                    inl_sio2_ctrl_set(0x3ac);
                    
                    inl_sio2_regN_set(0, pio_element);
                    inl_sio2_regN_set(1, 0);

                    //Place data TX FIFO
                    for (int i = 0; i < pio_size; i++) {
                        inl_sio2_data_out(buffer[bytes_done + i]);
                    }

                    bytes_done += pio_size;
                    pio_size = 0;

                    //Start the transfer
                    inl_sio2_ctrl_set(inl_sio2_ctrl_get() | 1);

                    //Set timeout alarm
                    SetAlarm(&timeout_2s, mmce_sio2_timeout_handler, &event_flag);

                    //Wait for completion or timeout
                    WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);
                    if (resbits & EF_SIO2_TRANSFER_TIMEOUT) {
                        DPRINTF("Detected transfer timeout, attempting to reset SIO2\n");
                        inl_sio2_ctrl_set(0x3ac); //Reset SIO2
                        ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
                        return -1;
                    }

                    //Cancel timeout alarm
                    CancelAlarm(mmce_sio2_timeout_handler,  &event_flag);

                    //Clear flags
                    ClearEventFlag(event_flag, ~(EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE));
                }
                polling = 1;
            }

        }
    }
    return 0;
}