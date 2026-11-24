
#include <dmacman.h>
#include <sio2regs.h>
#include <thbase.h>

#include "ioplib.h"
#include "irx_imports.h"

#include "mmce_sio2.h"
#include "sio2man_hook.h"

#include "module_debug.h"

static int mmce_port;

//Port ctrl settings used for all transfers
static u32 mmce_sio2_port_ctrl1;
static u32 mmce_sio2_port_ctrl2;

iop_sys_clock_t timeout_200ms;
iop_sys_clock_t timeout_1s;
iop_sys_clock_t timeout_2s;

static void mmce_sio2_clear_queue_and_settle(void)
{
    volatile int delay;

    for (int i = 0; i < 16; i++) {
        inl_sio2_regN_set(i, 0);
    }

    // Allow the device's /CS interrupt to reset its PIO before the next packet.
    for (delay = 0; delay < 200; delay++) {
        __asm__ __volatile__("" : : : "memory");
    }
}

static void mmce_sio2_reset_and_settle(void)
{
    inl_sio2_ctrl_set(0x0ac);
    mmce_sio2_clear_queue_and_settle();
}

static int mmce_sio2_wait_for_transfer(iop_sys_clock_t *timeout)
{
    iop_sys_clock_t start;
    iop_sys_clock_t current;

    GetSystemTime(&start);

    while ((inl_sio2_stat6c_get() & 0x1000) == 0) {
        u32 elapsed_lo;
        u32 elapsed_hi;

        GetSystemTime(&current);
        elapsed_lo = current.lo - start.lo;
        elapsed_hi = current.hi - start.hi - (current.lo < start.lo);
        if (elapsed_hi > timeout->hi ||
            (elapsed_hi == timeout->hi && elapsed_lo >= timeout->lo)) {
            DPRINTF("SIO2 transfer timed out\n");
            mmce_sio2_reset_and_settle();
            inl_sio2_stat_set(0x3);
            return -1;
        }
    }

    inl_sio2_stat_set(0x3);
    mmce_sio2_clear_queue_and_settle();
    return 0;
}

int mmce_sio2_init()
{
    //Install necessary hooks
    if (sio2man_hook_init() == -1) {
        DPRINTF("Failed to init sio2man hook\n");
        return -1;
    }

    //Ensure IOP DMA channels are enabled
    sceSetDMAPriority(IOP_DMAC_SIO2in, 3);
    sceSetDMAPriority(IOP_DMAC_SIO2out, 3);
    sceEnableDMAChannel(IOP_DMAC_SIO2in);
    sceEnableDMAChannel(IOP_DMAC_SIO2out);

    //200ms
    timeout_200ms.hi = 0x0;
    timeout_200ms.lo = 0x708000;

    //1s
    timeout_1s.hi = 0x0;
    timeout_1s.lo = 0x2328000;

    //2s
    timeout_2s.hi = 0x0;
    timeout_2s.lo = 0x4650000;

    mmce_sio2_port_ctrl1 =
        PCTRL0_ATT_LOW_PER(0x5)      |
        PCTRL0_ATT_MIN_HIGH_PER(0x0) |
        PCTRL0_BAUD0_DIV(0x2)        |
        PCTRL0_BAUD1_DIV(0xff);

    mmce_sio2_port_ctrl2 =
        PCTRL1_ACK_TIMEOUT_AFTER(0xffff)        |
        PCTRL1_WAIT_CYCLES_AFTER_ACK_LOW(0x5)   |
        PCTRL1_UNK24(0x0)                       |
        PCTRL1_IF_MODE_SPI_DIFF(0x0);

    return 0;
}

void mmce_sio2_deinit()
{
    sio2man_hook_deinit();
}

void mmce_sio2_lock()
{
    //Lock sio2man driver so we can use it exclusively
    sio2man_hook_sio2_lock();

    mmce_sio2_clear_queue_and_settle();

    //Copy port ctrl settings to SIO2 registers
    inl_sio2_portN_ctrl1_set(mmce_port, mmce_sio2_port_ctrl1);
    inl_sio2_portN_ctrl2_set(mmce_port, mmce_sio2_port_ctrl2);
}

void mmce_sio2_unlock()
{
    //Restore SIO2MAN's interrupt settings before releasing its lock.
    inl_sio2_ctrl_set(0x03bc);
    mmce_sio2_clear_queue_and_settle();

    //Unlock sio2man driver
    sio2man_hook_sio2_unlock();
}

void mmce_sio2_set_port(int port)
{
    mmce_port = port;
}

int mmce_sio2_get_port()
{
    return mmce_port;
}

void mmce_sio2_update_ack_wait_cycles(int cycles)
{
    DPRINTF("mmceman: setting cycles to: 0x%x\n", cycles);

    mmce_sio2_port_ctrl2 =
        PCTRL1_ACK_TIMEOUT_AFTER(0xffff)        |
        PCTRL1_WAIT_CYCLES_AFTER_ACK_LOW(cycles)|
        PCTRL1_UNK24(0x0)                       |
        PCTRL1_IF_MODE_SPI_DIFF(0x0);
}

void mmce_sio2_set_use_alarm(int value)
{
    (void)value;
}

int mmce_sio2_tx_rx_pio(u8 tx_size, u8 rx_size, u8 *tx_buf, u8 *rx_buf, iop_sys_clock_t *timeout)
{
    //Reset SIO2, clear transfer queue and settle device without enabling SIO2 interrupts.
    mmce_sio2_reset_and_settle();

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
                        TR_CTRL_TX_DATA_SZ(tx_size) |
                        TR_CTRL_RX_DATA_SZ(rx_size));


    //Copy data to TX FIFO
    for (int i = 0; i < tx_size; i++) {
        inl_sio2_data_out(tx_buf[i]);
    }

    //Start transfer without enabling interrupts.
    inl_sio2_ctrl_set(0x0a1);

    if (mmce_sio2_wait_for_transfer(timeout) < 0) {
        return -1;
    }

    //Copy data out of RX FIFO
    for (int i = 0; i < rx_size; i++) {
        rx_buf[i] = inl_sio2_data_in();
    }

    return 0;
}

//Simplified read for sectors / MMCEDRV
int mmce_sio2_rx_dma(u8 *buffer, u32 size)
{
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
        //Reset SIO2, clear transfer queue and settle device without enabling interrupts.
        mmce_sio2_reset_and_settle();

        elements_do = elements > 16 ? 16 : elements;

        //Copy DMA transfer elements to SIO2 registers
        for (int i = 0; i < elements_do; i++) {
            inl_sio2_regN_set(i, dma_element);
        }

        //Start DMA transfer
        sceSetSliceDMA(IOP_DMAC_SIO2out, &buffer[elements_done * 256], 0x100 >> 2, elements_do, DMAC_TO_MEM);
        sceStartDMA(IOP_DMAC_SIO2out);

        //Start and poll the transfer without enabling interrupts.
        inl_sio2_ctrl_set(0x0a1);
        if (mmce_sio2_wait_for_transfer(&timeout_2s) < 0) {
            return -1;
        }

        elements -= elements_do;
        elements_done += elements_do;
    }

    return 0;
}

//Mixed DMA / PIO RX transfer
int mmce_sio2_rx_mixed(u8 *buffer, u32 size)
{
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
        //Reset SIO2, clear transfer queue and settle device without enabling interrupts.
        mmce_sio2_reset_and_settle();

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

                offset_pio = bytes_done;
                bytes_done += pio_size;
            }
        }

        //Start DMA transfer if needed
        if (elements_do != 0) {
            sceSetSliceDMA(IOP_DMAC_SIO2out, &buffer[offset], 0x100 >> 2, elements_do, DMAC_TO_MEM);
            sceStartDMA(IOP_DMAC_SIO2out);
        }

        //Start and poll the transfer without enabling interrupts.
        inl_sio2_ctrl_set(0x0a1);
        if (mmce_sio2_wait_for_transfer(&timeout_2s) < 0) {
            return -1;
        }

        elements -= elements_do;
    }

    //Copy data from RX FIFO
    if (pio_size != 0x0) {
        for (int i = 0; i < pio_size; i++) {
            buffer[offset_pio + i] = inl_sio2_data_in();
        }
    }

    return 0;
}

int mmce_sio2_tx_mixed(u8 *buffer, u32 size)
{
    int res;

    //dma element count (round down)
    u32 elements = size / 256;

    //remainder < 256
    u32 pio_size = size - (elements * 256);

    u32 bytes_done = 0;
    u32 elements_do = 0;

    u8 waiting = 1;
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

    //used for all elements less than 256 bytes in size
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
        //Waiting stage
        if (waiting == 1) {
            res = mmce_sio2_tx_rx_pio(0, 2, NULL, rx_buf, &timeout_2s);
            if (res == -1) {
                DPRINTF("%s ERROR: Timed out waiting for ready\n", __func__);
                return -1;
            }

            if (bytes_done >= size)
                break;

            //Move to transfer stage
            waiting = 0;

        //Transfer stage
        } else if (waiting == 0) {
            elements_do = elements > 16 ? 16 : elements;

            //Full 4KB DMA transfer
            if (elements_do == 16) {
                //Reset SIO2, clear transfer queue and settle device without enabling interrupts.
                mmce_sio2_reset_and_settle();

                for (int i = 0; i < elements_do; i++) {
                    inl_sio2_regN_set(i, dma_element);
                }

                sceSetSliceDMA(IOP_DMAC_SIO2in, &buffer[bytes_done], 0x100 >> 2, elements_do, DMAC_FROM_MEM);
                sceStartDMA(IOP_DMAC_SIO2in);

                bytes_done += elements_do * 256;
                elements -= elements_do;

                //Start and poll the transfer without enabling interrupts.
                inl_sio2_ctrl_set(0x0a1);
                if (mmce_sio2_wait_for_transfer(&timeout_2s) < 0) {
                    return -1;
                }

                waiting = 1;

            //Sub 4KB DMA transfer
            } else {
                //Remaining DMA elements
                if (elements_do != 0) {

                    //Reset SIO2, clear transfer queue and settle device without enabling interrupts.
                    mmce_sio2_reset_and_settle();

                    for (int i = 0; i < elements_do; i++) {
                        inl_sio2_regN_set(i, dma_element);
                    }

                    sceSetSliceDMA(IOP_DMAC_SIO2in, &buffer[bytes_done], 0x100 >> 2, elements_do, DMAC_FROM_MEM);
                    sceStartDMA(IOP_DMAC_SIO2in);

                    bytes_done += elements_do * 256;
                    elements -= elements_do;

                    //Start and poll the transfer without enabling interrupts.
                    inl_sio2_ctrl_set(0x0a1);
                    if (mmce_sio2_wait_for_transfer(&timeout_2s) < 0) {
                        return -1;
                    }

                }
                //PIO element
                if (pio_size != 0) {
                    
                    //Reset SIO2, clear transfer queue and settle device without enabling interrupts.
                    mmce_sio2_reset_and_settle();
                    
                    inl_sio2_regN_set(0, pio_element);

                    //Place data TX FIFO
                    for (int i = 0; i < pio_size; i++) {
                        inl_sio2_data_out(buffer[bytes_done + i]);
                    }

                    bytes_done += pio_size;
                    pio_size = 0;

                    //Start and poll the transfer without enabling interrupts.
                    inl_sio2_ctrl_set(0x0a1);
                    if (mmce_sio2_wait_for_transfer(&timeout_2s) < 0) {
                        return -1;
                    }

                }
                waiting = 1;
            }

        }
    }
    return 0;
}