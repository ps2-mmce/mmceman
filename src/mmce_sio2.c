#include "ioplib.h"
#include "irx_imports.h"
#include "sio2regs.h"

#include "module_debug.h"

#include "mmce_sio2.h"
#include "mmce_cmds.h"

#define SecrSetMcCommandHandler_Expnum 0x04

static sio2_transfer_data_t sio2packet;
static int mmce_port;
static int mmce_slot;

/* El Isra's SECRMAN Hook */
typedef int (*McCommandHandler_t)(int port, int slot, sio2_transfer_data_t *sio2_trans_data); // to hook into secrman setter
typedef void (*SecrSetMcCommandHandler_hook_t)(McCommandHandler_t handler);

SecrSetMcCommandHandler_hook_t ORIGINAL_SecrSetMcCommandHandler = NULL;
static McCommandHandler_t McCommandHandler = NULL;

void HOOKED_SecrSetMcCommandHandler(McCommandHandler_t handler)
{
    DPRINTF("%s: handler ptr 0x%p\n",__FUNCTION__, handler);
    McCommandHandler = handler;
    ORIGINAL_SecrSetMcCommandHandler(handler); //we kept the original function to call it from here... else, SECRMAN wont auth cards...
}

int mmceman_sio2_hook_secrman()
{
    if (ioplib_getByName("mcman") != NULL)
    {
        DPRINTF("MCMAN FOUND. Must be loaded after this module to intercept the McCommandHandler\n");
        return -1;
    }

    iop_library_t * SECRMAN = ioplib_getByName("secrman");
    if (SECRMAN == NULL)
    {
        DPRINTF("SECRMAN not found\n");
        return -1;
    }
    DPRINTF("Found SECRMAN. version 0x%X\n", SECRMAN->version);

    ORIGINAL_SecrSetMcCommandHandler = (SecrSetMcCommandHandler_hook_t)ioplib_hookExportEntry(SECRMAN, SecrSetMcCommandHandler_Expnum, HOOKED_SecrSetMcCommandHandler);
    if (ORIGINAL_SecrSetMcCommandHandler == NULL)
    {
        DPRINTF("Error hooking into SecrSetMcCommandHandler\n");
        return -1;
    } else {
        DPRINTF("Hooked SecrSetMcCommandHandler (new one:0x%p, old one 0x%p)\n", HOOKED_SecrSetMcCommandHandler, ORIGINAL_SecrSetMcCommandHandler);
    }

    return 0;
}

int mmceman_sio2_unhook_secrman()
{
    DPRINTF("Restoring SECRMAN callback setter\n");
    iop_library_t * SECRMAN = ioplib_getByName("secrman");
    ioplib_hookExportEntry(SECRMAN, SecrSetMcCommandHandler_Expnum, ORIGINAL_SecrSetMcCommandHandler);
    return 0;
}

void mmceman_sio2_set_port(int port)
{
    mmce_port = port;
    mmce_slot = 0; //TODO: multitap stuff

    //update global transfer struct
    memset(sio2packet.port_ctrl1, 0, sizeof(sio2packet.port_ctrl1));
    memset(sio2packet.port_ctrl2, 0, sizeof(sio2packet.port_ctrl2));

    //configure baud and other common parameters
    //baud0 div = 0x02 (24MHz)
    //baud1 div = 0xff (~200KHz), unused.
    sio2packet.port_ctrl1[mmce_port] = PCTRL0_ATT_LOW_PER(0x5)      |
                                       PCTRL0_ATT_MIN_HIGH_PER(0x5) |
                                       PCTRL0_BAUD0_DIV(0x2)        |
                                       PCTRL0_BAUD1_DIV(0xFF);
 
    //ACK_TIMEOUT should allow for around ~2.5ms of delay between bytes in a single transfer
    sio2packet.port_ctrl2[mmce_port] = PCTRL1_ACK_TIMEOUT_PER(0xffff) |
                                       PCTRL1_INTER_BYTE_PER(0x5)     |
                                       PCTRL1_UNK24(0x0)              |
                                       PCTRL1_IF_MODE_SPI_DIFF(0x0);
}

int mmceman_sio2_send(u8 in_size, u8 out_size, u8 *in_buf, u8 *out_buf)
{
    sio2packet.in_dma.addr = NULL;
    sio2packet.out_dma.addr = NULL;

    sio2packet.in_size = in_size;
    sio2packet.out_size = out_size;

    sio2packet.in = in_buf;    //MEM -> SIO2
    sio2packet.out = out_buf;  //SIO2 -> MEM

    //configure single transfer (up to 255 bytes)
    sio2packet.regdata[0] = TR_CTRL_PORT_NR(mmce_port)  |
                            TR_CTRL_PAUSE(0)            |
                            TR_CTRL_TX_MODE_PIO_DMA(0)  |
                            TR_CTRL_RX_MODE_PIO_DMA(0)  |
                            TR_CTRL_NORMAL_TR(1)        |
                            TR_CTRL_SPECIAL_TR(0)       |
                            TR_CTRL_BAUD_DIV(0)         |
                            TR_CTRL_WAIT_ACK_FOREVER(0) |
                            TR_CTRL_TX_DATA_SZ(in_size) |
                            TR_CTRL_RX_DATA_SZ(out_size);
    sio2packet.regdata[1] = 0x0;

    //execute SIO2 transfer
    if (McCommandHandler(mmce_port, mmce_slot, &sio2packet) == 0) {
        DPRINTF("McCommandHandler failed\n");
    }

    if ((sio2packet.stat6c & 0x8000) != 0)
        return -1;

    return 0;
}

int mmceman_sio2_read_raw(u32 size, u8 *buffer)
{
    //dma element count (round down)
    u32 elements = size / 256;

    //remainder < 256
    u32 pio_size = size - (elements * 256);

    u32 bytes_done = 0;
    u32 elements_do = 0;
    int elements_set = -1;

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

    memset(sio2packet.regdata, 0, sizeof(sio2packet.regdata));

    sio2packet.in_dma.addr = NULL;
    sio2packet.out_dma.addr = NULL;

    sio2packet.in_size = 0; 
    sio2packet.out_size = 0;

    sio2packet.in = NULL;   //MEM -> SIO2
    sio2packet.out = NULL;  //SIO2 -> MEM

    sio2packet.out_dma.size = 0x40;

    while (bytes_done < size) {

        elements_do = elements > 16 ? 16 : elements;

        sio2packet.out_dma.addr = &buffer[bytes_done];
        sio2packet.out_dma.count = elements_do;

        //Avoid reassigning values
        if (elements_do != elements_set) {
            if (elements_set == -1) {
                for (int i = 0; i < elements_do; i++) {
                    sio2packet.regdata[i] = dma_element;
                }
            }

            bytes_done += elements_do * 256;
            elements_set = elements_do;

            if (elements_do < 16) {
                if (pio_size != 0x0) {
                    sio2packet.regdata[elements_do] = pio_element;
                    sio2packet.in_size = 0;
                    sio2packet.out_size = pio_size;

                    sio2packet.in = NULL;
                    sio2packet.out = &buffer[bytes_done];

                    bytes_done += pio_size;

                    if (elements_do + 1 < 16) {
                        sio2packet.regdata[elements_do + 1] = 0x0; //Term transfer queue
                    }

                } else {
                    sio2packet.regdata[elements_do] = 0x0;
                }
            }

        } else {
            bytes_done += elements_do * 256;
        }

        elements -= elements_do;
        
        /*for (int i = 0; i < 16; i++) {
            printf("reg %i: 0x%x\n", i, sio2packet.regdata[i]);
        }*/

        McCommandHandler(3, 0, &sio2packet);
        
        if ((sio2packet.stat6c & 0x8000) != 0)
            return -1;
    }

    return 0;
}

int mmceman_sio2_write_raw(u32 size, u8 *buffer)
{
    int rv;

    //dma element count (round down)
    u32 elements = size / 256;

    //remainder < 256
    u32 pio_size = size - (elements * 256);

    u32 bytes_done = 0;
    u32 elements_do = 0;

    u8 polling = 1;

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

    memset(sio2packet.regdata, 0, sizeof(sio2packet.regdata));

    sio2packet.in_dma.addr = NULL;
    sio2packet.out_dma.addr = NULL;

    sio2packet.in_size = 0; 
    sio2packet.out_size = 0;

    sio2packet.in = NULL;   //MEM -> SIO2
    sio2packet.out = NULL;  //SIO2 -> MEM

    sio2packet.in_dma.size = 0x40;

    while (1) {
        if (polling == 1) {
            rv = mmceman_sio2_wait_equal(1, 128000);
            if (rv == -1) {
                DPRINTF("%s ERROR: Polling timeout reached\n", __func__);
                return -1;
            }

            if (bytes_done >= size)
                break;

            polling = 0;

        } else if (polling == 0) {
            elements_do = elements > 16 ? 16 : elements;
            
            //Full 4KB DMA transfer
            if (elements_do == 16) {
                sio2packet.in_dma.addr = &buffer[bytes_done];
                sio2packet.in_dma.count = elements_do;

                for (int i = 0; i < elements_do; i++) {
                    sio2packet.regdata[i] = dma_element;
                }

                bytes_done += elements_do * 256;
                elements -= elements_do;

                McCommandHandler(3, 0, &sio2packet);

                //Move back to polling stage
                polling = 1;
            
            //Sub-4KB DMA transfer
            } else {

                if (elements_do != 0) {
                    sio2packet.in_dma.addr = &buffer[bytes_done];
                    sio2packet.in_dma.count = elements_do;

                    for (int i = 0; i < elements_do; i++) {
                        sio2packet.regdata[i] = dma_element;
                    }
                    sio2packet.regdata[elements_do] = 0x0; //Term transfer queue

                    bytes_done += elements_do * 256;
                    elements -= elements_do;

                    McCommandHandler(3, 0, &sio2packet);
                }

                if (pio_size != 0) {
                    sio2packet.regdata[0] = pio_element;
                    sio2packet.regdata[1] = 0;

                    sio2packet.in_size = pio_size;
                    sio2packet.out_size = 0x0;

                    sio2packet.in = &buffer[bytes_done];
                    sio2packet.out = NULL;

                    sio2packet.in_dma.addr = NULL;
                    sio2packet.in_dma.count = 0;

                    bytes_done += pio_size;
                    pio_size = 0;

                    McCommandHandler(3, 0, &sio2packet);
                }

                polling = 1;
            }
        }
    }
    return 0;
}

int mmceman_sio2_wait_equal(u8 value, u32 timeout)
{ 
    u8 rdbuf[0x3];
    rdbuf[0x1] = 0;

    //memset(sio2packet.regdata, 0, sizeof(sio2packet.regdata));

    sio2packet.in_dma.addr = NULL;
    sio2packet.out_dma.addr = NULL;

    sio2packet.in_size = 0; 
    sio2packet.out_size = 2;

    sio2packet.in = NULL;   //MEM -> SIO2
    sio2packet.out = &rdbuf;  //SIO2 -> MEM

    sio2packet.regdata[0] = TR_CTRL_PORT_NR(mmce_port)  |
                            TR_CTRL_PAUSE(0)            |
                            TR_CTRL_TX_MODE_PIO_DMA(0)  |
                            TR_CTRL_RX_MODE_PIO_DMA(0)  |
                            TR_CTRL_NORMAL_TR(1)        |
                            TR_CTRL_SPECIAL_TR(0)       |
                            TR_CTRL_BAUD_DIV(0)         |
                            TR_CTRL_WAIT_ACK_FOREVER(0) |
                            TR_CTRL_TX_DATA_SZ(0)       |
                            TR_CTRL_RX_DATA_SZ(2);
    sio2packet.regdata[1] = 0x0;

    while (timeout > 0 && value != rdbuf[0x1]) {
        McCommandHandler(mmce_port, 0, &sio2packet);

        if ((sio2packet.stat6c & 0x8000) != 0) {
            return -1;
        }

        timeout--;
    }

    return 0;
}