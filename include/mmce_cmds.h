#ifndef MMCE_CMDS_H
#define MMCE_CMDS_H

#include <tamtypes.h>

#define MMCEMAN_ID 0x8B
#define MMCEMAN_RESERVED 0xFF
#define MMCEMAN_REPLY_CONST 0xAA

enum mmceman_cmds {
    MMCEMAN_CMD_PING = 0x1,
    MMCEMAN_CMD_GET_STATUS,
    MMCEMAN_CMD_GET_CARD,
    MMCEMAN_CMD_SET_CARD,
    MMCEMAN_CMD_GET_CHANNEL,
    MMCEMAN_CMD_SET_CHANNEL,
    MMCEMAN_CMD_GET_GAMEID,
    MMCEMAN_CMD_SET_GAMEID,
    MMCEMAN_IOCTL_PROBE_PORT,
};

//Called through ioctl
int mmceman_cmd_ping(void);
int mmceman_cmd_get_status(void);
int mmceman_cmd_get_card(void);
int mmceman_cmd_set_card(u8 type, u8 mode, u16 num);
int mmceman_cmd_get_channel(void);
int mmceman_cmd_set_channel(u8 mode, u16 num);
int mmceman_cmd_get_gameid(void *ptr);
int mmceman_cmd_set_gameid(void *ptr);

#endif