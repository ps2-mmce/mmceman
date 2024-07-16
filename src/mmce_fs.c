
#include <tamtypes.h>
#include <errno.h>
#include <ioman.h>
#include <string.h>

#include "module_debug.h"

#include "mmce_sio2.h"
#include "mmce_cmds.h"
#include "mmce_fs.h"

#define NOT_SUPPORTED_OP (void*)&not_supported_operation
static int not_supported_operation() {
    return -ENOTSUP;
}

static int mmceman_fs_fds[MMCEMAN_MAX_FD];
static int mmceman_gameid_fd = 255;
static int mmceman_dev_fd = 254;

static int *mmceman_fs_find_free_handle(void) {
    for (int i = 0; i < MMCEMAN_MAX_FD; i++)
    {
        if (mmceman_fs_fds[i] < 0)
            return &mmceman_fs_fds[i];
    }
    return NULL;
}

int mmceman_fs_init(iop_device_t *f)
{
    DPRINTF("%s: init\n", __func__);
    memset(mmceman_fs_fds, -1, sizeof(mmceman_fs_fds));
    return 0;
}

int mmceman_fs_deinit(iop_device_t *f)
{
    return 0;
}

int mmceman_fs_open(iop_file_t *file, const char *name, int flags)
{
    int res;

    u8 wrbuf[0x5];  
    u8 rdbuf[0x3];

    DPRINTF("%s name: %s flags: 0x%x\n", __func__, name, flags);

    //ioctl dev
    if (strcmp(name, "dev") == 0) {
        file->privdata = &mmceman_dev_fd;
        return 0;
    }

    //reserved gameid fd 
    if (strcmp(name, "gameid") == 0) {
        file->privdata = &mmceman_gameid_fd;
        return 0;
    }

    u8 packed_flags = 0;
    u8 filename_len = strlen(name) + 1;

    packed_flags  = (flags & 3) - 1;        //O_RDONLY, O_WRONLY, O_RDWR
    packed_flags |= (flags & 0x100) >> 5;   //O_APPEND
    packed_flags |= (flags & 0xE00) >> 4;   //O_CREATE, O_TRUNC, O_EXCL

    wrbuf[0x0] = MMCEMAN_ID;                //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_OPEN;       //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //Reserved
    wrbuf[0x3] = packed_flags;              //8 bit packed flags
    wrbuf[0x4] = 0xff;

    //Packet #1: Command and flags
    res = mmceman_sio2_send(0x5, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2: Filename
    res = mmceman_sio2_send(filename_len, 0x0, name, NULL);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #3 - n: Polling for ready 
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #4 - File handle
    res = mmceman_sio2_send(0x0, 0x3, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //TODO: Move this to the top
    file->privdata = (int*)mmceman_fs_find_free_handle();

    if ((int*)file->privdata != NULL) {
        if (rdbuf[0x1] != 0xff) {
            *(int*)file->privdata = rdbuf[0x1];
            DPRINTF("%s Opened fd: %i\n", __func__, rdbuf[0x1]);
        } else {
            DPRINTF("%s ERROR: Got fd %i: %i\n", __func__, rdbuf[0x1]);
            return -1;
        }
    } else {
        DPRINTF("%s ERROR: No free file handles available\n", __func__);
        return -1;
    }

    return 0;
}

int mmceman_fs_close(iop_file_t *file)
{
    int res = 0;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];
 
    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: file->privdata NULL\n", __func__);
        return -1;
    }

    if (*(int*)file->privdata == mmceman_dev_fd || *(int*)file->privdata == mmceman_gameid_fd) {
        return 0;
    }

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    wrbuf[0x0] = MMCEMAN_ID;                //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_CLOSE;      //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //Reserved
    wrbuf[0x3] = (u8)*(int*)file->privdata; //File descriptor
    
    //Packet #1: Command and file descriptor
    res = mmceman_sio2_send(0x4, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #3: Return value
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] == 0x0) {
        *(int*)file->privdata = -1;
    } else {
        res = rdbuf[0x1];
        DPRINTF("%s ERROR: got return value %i\n", __func__, res);
    }

    return res;
}

int mmceman_fs_read(iop_file_t *file, void *ptr, int size)
{
    int res;
    int bytes_read;

    u8 wrbuf[0xA];
    u8 rdbuf[0xA];

    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: file->privdata NULL\n", __func__);
        return -1;
    }

    DPRINTF("%s fd: %i, size: %i\n", __func__, (u8)*(int*)file->privdata, size);

    //Reserved dev fd
    if (*(int*)file->privdata == mmceman_dev_fd) {
        return 0;
    }

    //Reserved gameid fd
    if (*(int*)file->privdata == mmceman_gameid_fd) {
        mmceman_cmd_get_gameid(ptr);
        return size;
    }

    wrbuf[0x0] = MMCEMAN_ID;                //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_READ;       //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //Reserved
    wrbuf[0x3] = 0x0;                       //Transfer mode (unimplemented)
    wrbuf[0x4] = (u8)*(int*)file->privdata; //File Descriptor 
    wrbuf[0x5] = (size & 0xFF000000) >> 24; //Size
    wrbuf[0x6] = (size & 0x00FF0000) >> 16;
    wrbuf[0x7] = (size & 0x0000FF00) >> 8;
    wrbuf[0x8] = (size & 0x000000FF);
    wrbuf[0x9] = 0xff;

    //Packet #1: Command, file descriptor, and size
    res = mmceman_sio2_send(0xA, 0xA, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    if (rdbuf[0x9] != 0x0) {
        DPRINTF("%s ERROR: P1 - Got bad return value from card, res %i\n", __func__, rdbuf[0x9]);
        return -1;
    }

    //Packet #2 - n: Raw read data
    res = mmceman_sio2_read_raw(size, ptr);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #n + 1: Bytes read
    mmceman_sio2_send(0x0, 0x6, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    bytes_read  = rdbuf[0x1] << 24;
    bytes_read |= rdbuf[0x2] << 16;
    bytes_read |= rdbuf[0x3] << 8;
    bytes_read |= rdbuf[0x4];

    if (bytes_read != size) {
        DPRINTF("%s ERROR: bytes read: %i, expected: %i\n", __func__, bytes_read, size);
    }

    return bytes_read;
}

int mmceman_fs_write(iop_file_t *file, void *ptr, int size)
{
    int res;
    int bytes_written;

    u8 wrbuf[0xA];
    u8 rdbuf[0xA];

    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: file->privdata NULL\n", __func__);
        return -1;
    }

    DPRINTF("%s fd: %i, size: %i\n", __func__, (u8)*(int*)file->privdata, size);

    //Reserved dev fd
    if (*(int*)file->privdata == mmceman_dev_fd) {
        return 0;
    }

    //Reserved gameid fd
    if (*(int*)file->privdata == mmceman_gameid_fd) {
        mmceman_cmd_set_gameid(ptr);
        return size;
    }

    wrbuf[0x0] = MMCEMAN_ID;                //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_WRITE;      //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //Reserved
    wrbuf[0x3] = 0x0;                       //Transfer mode (unimplemented)
    wrbuf[0x4] = (u8)*(int*)file->privdata; //File Descriptor 
    wrbuf[0x5] = (size & 0xFF000000) >> 24;
    wrbuf[0x6] = (size & 0x00FF0000) >> 16;
    wrbuf[0x7] = (size & 0x0000FF00) >> 8;
    wrbuf[0x8] = (size & 0x000000FF);
    wrbuf[0x9] = 0xff;

    //Packet #1: Command, file descriptor, and size
    res = mmceman_sio2_send(0xA, 0xA, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    if (rdbuf[0x9] != 0x0) {
        DPRINTF("%s ERROR: P1 - Got bad return value from card 0x%x\n", __func__, rdbuf[0x9]);
        return -1;
    }

    //Packet #2 - n: Raw write data
    res = mmceman_sio2_write_raw(size, ptr);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packets #n + 1: Bytes written
    res = mmceman_sio2_send(0x0, 0x6, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    bytes_written  = rdbuf[0x1] << 24;    
    bytes_written |= rdbuf[0x2] << 16;   
    bytes_written |= rdbuf[0x3] << 8;   
    bytes_written |= rdbuf[0x4];

    if (bytes_written != size) {
        DPRINTF("%s bytes written: %i, expected: %i\n", __func__, bytes_written, size);
    }

    return bytes_written;
}

int mmceman_fs_lseek(iop_file_t *file, int offset, int whence)
{
    int res;
    int position = -1;

    u8 wrbuf[0x9];
    u8 rdbuf[0x5];

    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: file->privdata NULL\n", __func__);
        return -1;
    }

    DPRINTF("%s fd: %i, offset: %i, whence: %i\n", __func__, (u8)*(int*)file->privdata, offset, whence);

    wrbuf[0x0] = MMCEMAN_ID;                    //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_LSEEK;          //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;              //Reserved
    wrbuf[0x3] = (u8)(*(int*)file->privdata);   //File descriptor
    wrbuf[0x4] = (offset & 0xFF000000) >> 24;   //Offset
    wrbuf[0x5] = (offset & 0x00FF0000) >> 16;
    wrbuf[0x6] = (offset & 0x0000FF00) >> 8;
    wrbuf[0x7] = (offset & 0x000000FF);
    wrbuf[0x8] = (u8)(whence);                  //Whence
    
    //Packet #1: Command, file descriptor, offset, and whence
    res = mmceman_sio2_send(0x9, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: Position
    res = mmceman_sio2_send(0x0, 0x5, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    position  = rdbuf[0x1] << 24;
    position |= rdbuf[0x2] << 16;
    position |= rdbuf[0x3] << 8;
    position |= rdbuf[0x4];

    DPRINTF("%s position %i\n", __func__, position);

    return position;
}

int mmceman_fs_ioctl(iop_file_t *file, int cmd, void *data)
{
    int res = 0;

    u8 type = 0;
    u8 mode = 0;
    u16 num = 0;

    u32 args = *(u32*)data;
    
    DPRINTF("%s cmd: %i, args: 0x%x\n", __func__, cmd, args);

    switch (cmd) {
        case MMCEMAN_CMD_PING:
            res = mmceman_cmd_ping();
        break;

        case MMCEMAN_CMD_GET_STATUS:
            res = mmceman_cmd_get_status();
        break;
        
        case MMCEMAN_CMD_GET_CARD:
            res = mmceman_cmd_get_card();
        break;
        
        case MMCEMAN_CMD_SET_CARD: 
            type = (args & 0xff000000) >> 24;
            mode = (args & 0x00ff0000) >> 16;
            num  = (args & 0x0000ffff);

            res = mmceman_cmd_set_card(type, mode, num);
        break;
        
        case MMCEMAN_CMD_GET_CHANNEL:
            res = mmceman_cmd_get_channel();
        break;

        case MMCEMAN_CMD_SET_CHANNEL:
            mode = (args & 0x00ff0000) >> 16;
            num  = (args & 0x0000ffff);
            res = mmceman_cmd_set_channel(mode, num);
        break;
    
        case MMCEMAN_IOCTL_PROBE_PORT:
            mmceman_sio2_set_port(2);

            DPRINTF("Pinging port 2 (MC1)\n");
            res = mmceman_cmd_ping();
            if (res != -1) {
                DPRINTF("Got valid response from MMCE in port 2 (MC1)\n"); 
                res = 2;
                break;
            }

            DPRINTF("Failed to get valid response from port 2 (MC1)\n");
            mmceman_sio2_set_port(3);

            DPRINTF("Pinging port 3 (MC2)\n");
            res = mmceman_cmd_ping();
            if (res != -1) {
                DPRINTF("Got valid response from MMCE in port 3 (MC2)\n");
                res = 3;
                break;
            }
            DPRINTF("Failed to get valid response from either port, check connection\n");
            res = 0;

        break;
    }
    return res;
}

/*Note: Due to a bug in FILEIO, mkdir will be called after remove unless
        sbv_patch_fileio is used. See ps2sdk/ee/sbv/src/patch_fileio.c */
int mmceman_fs_remove(iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    DPRINTF("%s name: %s\n", __func__, name);

    u8 filename_len = strlen(name) + 1;

    wrbuf[0x0] = MMCEMAN_ID;            //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_REMOVE; //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    //Packet #1: Command
    res = mmceman_sio2_send(0x4, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2: Filename
    res = mmceman_sio2_send(filename_len, 0x0, name, NULL);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #3 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: Return value
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to remove %s, return value %i\n", __func__, name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}

int mmceman_fs_mkdir(iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    DPRINTF("%s name: %s\n", __func__, name);

    u8 dir_len = strlen(name) + 1;

    wrbuf[0x0] = MMCEMAN_ID;            //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_MKDIR;  //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    //Packet #1: Command
    res = mmceman_sio2_send(0x4, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2: Dirname
    res = mmceman_sio2_send(dir_len, 0x0, name, NULL);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #3 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: Return value
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to mkdir %s, return value %i\n", __func__, name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}

int mmceman_fs_rmdir(iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    DPRINTF("%s name: %s\n", __func__, name);

    u8 dir_len = strlen(name) + 1;

    wrbuf[0x0] = MMCEMAN_ID;            //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_RMDIR;  //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    //Packet #1: Command
    res = mmceman_sio2_send(0x4, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2: Dirname
    res = mmceman_sio2_send(dir_len, 0x0, name, NULL);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #3 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: Return value
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to rmdir %s, return value %i\n", __func__, name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}

int mmceman_fs_dopen(iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x5];  
    u8 rdbuf[0x3];

    DPRINTF("%s name: %s\n", __func__, name);

    u8 dir_len = strlen(name) + 1;
    
    wrbuf[0x0] = MMCEMAN_ID;            //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_DOPEN;  //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    //Packet #1: Command
    res = mmceman_sio2_send(0x4, 0x2, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2: Dirname
    res = mmceman_sio2_send(dir_len, 0x0, name, NULL);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #3 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: File handle
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    file->privdata = (int*)mmceman_fs_find_free_handle();

    if ((int*)file->privdata != NULL) {
        if (rdbuf[0x1] != 0x0 && rdbuf[0x1] != 0xff) {
            *(int*)file->privdata = rdbuf[0x1];
        }
    } else {
        DPRINTF("%s ERROR: No free file handles available\n", __func__);
        return -1;
    }

    return 0;
}

int mmceman_fs_dclose(iop_file_t *file)
{
    int res;

    u8 wrbuf[0x5];
    u8 rdbuf[0x5];
 
    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: file->privdata NULL\n", __func__);
        return -1;
    }

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    wrbuf[0x0] = MMCEMAN_ID;                //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_DCLOSE;     //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //Reserved
    wrbuf[0x3] = (u8)*(int*)file->privdata; //File descriptor
    
    //Packet #1: Command and file descriptor
    res = mmceman_sio2_send(0x3, 0x5, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    //Packet #2 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: Return value
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to close dir %i, return value %i\n", __func__, (u8)*(int*)file->privdata, rdbuf[0x1]);
        return -1;
    }

    *(int*)file->privdata = -1;

    return 0;
}

int mmceman_fs_dread(iop_file_t *file, io_dirent_t *dirent)
{
    int res;

    u8 wrbuf[0x5];
    u8 rdbuf[0x2B];

    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: file->privdata NULL\n", __func__);
        return -1;
    }

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    u8 filename_len = 0;

    wrbuf[0x0] = MMCEMAN_ID;                //Identifier
    wrbuf[0x1] = MMCEMAN_CMD_FS_DREAD;      //Command
    wrbuf[0x2] = MMCEMAN_RESERVED;          //Reserved
    wrbuf[0x3] = (u8)*(int*)file->privdata; //File descriptor
    wrbuf[0x4] = 0xff;

    //Packet #1: Command and file descriptor
    res = mmceman_sio2_send(0x5, 0x5, wrbuf, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCEMAN_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCEMAN_REPLY_CONST);
        return -1;
    }

    if (rdbuf[0x4] != 0) {
        DPRINTF("%s ERROR: Returned %i, Expected 0\n", __func__, rdbuf[0x4]);
        return -1;
    }

    //Packet #2 - n: Polling ready
    res = mmceman_sio2_wait_equal(1, MMCEMAN_FS_WAIT_TIMEOUT);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Polling timedout\n", __func__);
        return -1;
    }

    //Packet #n + 1: io_stat_t and filename len
    res = mmceman_sio2_send(0x0, 0x2A, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Could cast but not sure about alignment?
    dirent->stat.mode  = rdbuf[0x1] << 24;
    dirent->stat.mode |= rdbuf[0x2] << 16;
    dirent->stat.mode |= rdbuf[0x3] << 8;
    dirent->stat.mode |= rdbuf[0x4];

    dirent->stat.attr  = rdbuf[0x5] << 24;
    dirent->stat.attr |= rdbuf[0x6] << 16;
    dirent->stat.attr |= rdbuf[0x7] << 8;
    dirent->stat.attr |= rdbuf[0x8];

    dirent->stat.size  = rdbuf[0x9] << 24;
    dirent->stat.size |= rdbuf[0xA] << 16;
    dirent->stat.size |= rdbuf[0xB] << 8;
    dirent->stat.size |= rdbuf[0xC];

    dirent->stat.ctime[0] = rdbuf[0xD];
    dirent->stat.ctime[1] = rdbuf[0xE];
    dirent->stat.ctime[2] = rdbuf[0xF];
    dirent->stat.ctime[3] = rdbuf[0x10];
    dirent->stat.ctime[4] = rdbuf[0x11];
    dirent->stat.ctime[5] = rdbuf[0x12];
    dirent->stat.ctime[6] = rdbuf[0x13];
    dirent->stat.ctime[7] = rdbuf[0x14];

    dirent->stat.atime[0] = rdbuf[0x15];
    dirent->stat.atime[1] = rdbuf[0x16];
    dirent->stat.atime[2] = rdbuf[0x17];
    dirent->stat.atime[3] = rdbuf[0x18];
    dirent->stat.atime[4] = rdbuf[0x19];
    dirent->stat.atime[5] = rdbuf[0x1A];
    dirent->stat.atime[6] = rdbuf[0x1B];
    dirent->stat.atime[7] = rdbuf[0x1C];

    dirent->stat.mtime[0] = rdbuf[0x1D];
    dirent->stat.mtime[1] = rdbuf[0x1E];
    dirent->stat.mtime[2] = rdbuf[0x1F];
    dirent->stat.mtime[3] = rdbuf[0x20];
    dirent->stat.mtime[4] = rdbuf[0x21];
    dirent->stat.mtime[5] = rdbuf[0x22];
    dirent->stat.mtime[6] = rdbuf[0x23];
    dirent->stat.mtime[7] = rdbuf[0x24];

    dirent->stat.hisize  = rdbuf[0x25] << 24;
    dirent->stat.hisize |= rdbuf[0x26] << 16;
    dirent->stat.hisize |= rdbuf[0x27] << 8;
    dirent->stat.hisize |= rdbuf[0x28];

    filename_len = rdbuf[0x29];

    //Packet #n + 2: Filename
    res = mmceman_sio2_send(0x0, filename_len, NULL, dirent->name);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    //Packet #n + 3: Padding, resevered, and term
    res = mmceman_sio2_send(0x0, 0x3, NULL, rdbuf);
    if (res == -1) {
        DPRINTF("%s ERROR: P5 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    return 0;
}

static iop_device_ops_t mmceman_fio_ops =
{
	&mmceman_fs_init, //init
	&mmceman_fs_deinit, //deinit
	NOT_SUPPORTED_OP, //format
	&mmceman_fs_open, //open
	&mmceman_fs_close, //close
	&mmceman_fs_read, //read
	&mmceman_fs_write, //write
	&mmceman_fs_lseek, //lseek
	&mmceman_fs_ioctl, //ioctl
	&mmceman_fs_remove, //remove
	&mmceman_fs_mkdir, //mkdir
	&mmceman_fs_rmdir, //rmdir
	&mmceman_fs_dopen, //dopen
	&mmceman_fs_dclose, //dclose
	&mmceman_fs_dread, //dread
	NOT_SUPPORTED_OP, //getstat
	NOT_SUPPORTED_OP, //chstat
};

static iop_device_t mmceman_dev =
{
	"mmce",
	IOP_DT_FS,
	1,
	"Filesystem access mmce",
	&mmceman_fio_ops, //fill this with an instance of iop_device_ops_t
};

int mmceman_fs_register(void) {
    DPRINTF("Registering %s device\n", mmceman_dev.name);
    DelDrv(mmceman_dev.name);
    
    if (AddDrv(&mmceman_dev)!= 0)
        return 0;
    
    return 1;
}

int mmceman_fs_unregister(void) {
    DelDrv(mmceman_dev.name);
    return 0;
}