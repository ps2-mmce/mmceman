#include <tamtypes.h>
#include <stdio.h>
#include <sysclib.h>


#include "module_debug.h"

#include "mmce_sio2.h"
#include "mmce_cmds.h"

int mmceman_cmd_ping(void)
{
    int res;

    u8 wrbuf[0x3];
    u8 rdbuf[0x7];

    wrbuf[0x0] = MMCEMAN_ID;          //identifier
    wrbuf[0x1] = MMCEMAN_CMD_PING;    //command
    wrbuf[0x2] = MMCEMAN_RESERVED;    //reserved byte

    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //bits 24-16: protocol ver
    //bits 16-8: product id
    //bits 8-0: revision id
    if (rdbuf[0x1] == MMCEMAN_REPLY_CONST) {
        res = rdbuf[0x3] << 16 | rdbuf[0x4] << 8 | rdbuf[0x5];
    } else {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        res = -1;
    }

    return res;
}

//TODO:
int mmceman_cmd_get_status(void)
{
    int res;

    u8 wrbuf[0x3];
    u8 rdbuf[0x6];

    wrbuf[0x0] = MMCEMAN_ID;                //identifier
    wrbuf[0x1] = MMCEMAN_CMD_GET_STATUS;    //command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //reserved byte
    
    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] == MMCEMAN_REPLY_CONST) {
        res = rdbuf[0x4];
    } else {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        res = -1;
    }

    return res;
}

int mmceman_cmd_get_card(void)
{
    int res;

    u8 wrbuf[0x3];
    u8 rdbuf[0x6];

    wrbuf[0x0] = MMCEMAN_ID;            //identifier
    wrbuf[0x1] = MMCEMAN_CMD_GET_CARD;  //command
    wrbuf[0x2] = MMCEMAN_RESERVED;      //reserved byte

    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] == MMCEMAN_REPLY_CONST) {
        res = rdbuf[0x3] << 8 | rdbuf[0x4];
    } else {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        res = -1;
    }

    return res;
}

int mmceman_cmd_set_card(u8 type, u8 mode, u16 num)
{
    int res;

    u8 wrbuf[0x8];
    u8 rdbuf[0x2];

    wrbuf[0x0] = MMCEMAN_ID;            //identifier
    wrbuf[0x1] = MMCEMAN_CMD_SET_CARD;  //command
    wrbuf[0x2] = MMCEMAN_RESERVED;      //reserved byte
    wrbuf[0x3] = type;                  //card type (0 = regular, 1 = boot)
    wrbuf[0x4] = mode;                  //set mode (num, next, prev)
    wrbuf[0x5] = num >> 8;              //card number upper 8 bits
    wrbuf[0x6] = num & 0xFF;            //card number lower 8 bits

    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    return 0;
}

int mmceman_cmd_get_channel(void)
{
    int res;

    u8 wrbuf[0x3];
    u8 rdbuf[0x6];

    wrbuf[0x0] = MMCEMAN_ID;                //identifier
    wrbuf[0x1] = MMCEMAN_CMD_GET_CHANNEL;   //command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //reserved byte

    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }
    
    if (rdbuf[0x1] == MMCEMAN_REPLY_CONST) {
        res = rdbuf[0x3] << 8 | rdbuf[0x4];
    } else {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        res = -1;
    }

    return res;
}

int mmceman_cmd_set_channel(u8 mode, u16 num)
{
    int res;

    u8 wrbuf[0x7];
    u8 rdbuf[0x2];

    wrbuf[0x0] = MMCEMAN_ID;                //identifier
    wrbuf[0x1] = MMCEMAN_CMD_SET_CHANNEL;   //command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //reserved byte
    wrbuf[0x3] = mode;                      //set mode (num, next, prev)
    wrbuf[0x4] = num >> 8;                  //channel number upper 8 bits    
    wrbuf[0x5] = num & 0xFF;                //channel number lower 8 bits

    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    return 0;
}

int mmceman_cmd_get_gameid(void *ptr)
{
    int res;

    u8 wrbuf[0x3];
    u8 rdbuf[0xFF]; //fixed packet size of 255 bytes

    wrbuf[0x0] = MMCEMAN_ID;             //identifier
    wrbuf[0x1] = MMCEMAN_CMD_GET_GAMEID; //command
    wrbuf[0x2] = MMCEMAN_RESERVED;       //reserved byte

    res = mmceman_sio2_send(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] == MMCEMAN_REPLY_CONST) {
        char* str = &rdbuf[0x4];
        strcpy(ptr, str);
        res = 0;
    } else {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        res = -1;
    }

    return res;
}

int mmceman_cmd_set_gameid(void *ptr)
{
    int res;

    u8 len = strlen(ptr) + 1;

    u8 wrbuf[0xFF];
    u8 rdbuf[0x2];

    wrbuf[0x0] = MMCEMAN_ID;             //identifier
    wrbuf[0x1] = MMCEMAN_CMD_SET_GAMEID; //command
    wrbuf[0x2] = MMCEMAN_RESERVED;       //reserved byte
    wrbuf[0x3] = len;                    //gameid length

    char *str = &wrbuf[0x4];
    strcpy(str, ptr);

    res = mmceman_sio2_send(len + 5, sizeof(rdbuf), wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    return 0;
}