#include <thsemap.h>
#include <thbase.h>

#include "module_debug.h"
#include "mmcedrv_config.h"

#include "fhi_fileid.h"

struct fhi_fileid fhi = {MODULE_SETTINGS_MAGIC};

static int mmce_io_sema;

extern s64 mmcedrv_lseek64(int fd, s64 offset, int whence);
extern int mmcedrv_read_sector(int fd, u32 sector, u32 count, void *buffer);
extern int mmcedrv_read(int fd, int size, void *ptr);
extern int mmcedrv_write(int fd, int size, const void *ptr);


//---------------------------------------------------------------------------
// FHI export #4
u32 fhi_size(int file_handle)
{
    if (file_handle < 0 || file_handle >= FHI_MAX_FILES)
        return 0;

    DPRINTF("%s(%d)\n", __func__, file_handle);

    return fhi.file[file_handle].size / 512;
}

//---------------------------------------------------------------------------
// FHI export #5
int fhi_read(int file_handle, void *buffer, unsigned int sector_start, unsigned int sector_count)
{
    int res = 0;
    int retries = 0;

    if (file_handle < 0 || file_handle >= FHI_MAX_FILES)
        return 0;

    DPRINTF("%s(%i, %u, 0x%p, %u)\n", __func__, file_handle, (unsigned int)sector_start, buffer, sector_count);

    WaitSema(mmce_io_sema);

    //ISO, use mmcedrv_sector_read
    if (file_handle == FHI_FID_CDVD) {

        //mmcedrv uses sector sizes of 2048
        sector_start = sector_start / 4;
        sector_count = sector_count / 4;

        do {
            res = mmcedrv_read_sector(fhi.file[file_handle].id, sector_start, sector_count, buffer);
            retries++;
        } while (res != sector_count && retries < 3);

        res = res * 4;

    //Other files (VMC/ATA/...), use lseek + read
    } else {
        mmcedrv_lseek64(fhi.file[file_handle].id, (s64)sector_start * 512, 0);
        res = mmcedrv_read(fhi.file[file_handle].id, sector_count * 512, buffer);

        if (res == sector_count * 512)
            res = 1;
        else
            res = 0;
    }
    SignalSema(mmce_io_sema);

    if (retries == 3) {
        DPRINTF("%s: Failed to read after 3 retires, sector: %u, count: %u, buffer: 0x%p\n", __func__, sector_start, sector_count, buffer);
    }

    return res;
}

//---------------------------------------------------------------------------
// FHI export #6
int fhi_write(int file_handle, const void *buffer, unsigned int sector_start, unsigned int sector_count)
{
    int res;

    if (file_handle < 0 || file_handle >= FHI_MAX_FILES)
        return 0;

    //Don't allow writing to DVD
    if (file_handle == FHI_FID_CDVD) {
        return 0;
    }

    DPRINTF("%s(%i, %u, 0x%p, %u)\n", __func__, file_handle, (unsigned int)sector_start, buffer, sector_count);

    WaitSema(mmce_io_sema);
    mmcedrv_lseek64(fhi.file[file_handle].id, (s64)sector_start * 512, 0);
    res = mmcedrv_write(fhi.file[file_handle].id, sector_count * 512, buffer);
    SignalSema(mmce_io_sema);

    if (res != sector_count * 512)
        return 0;

    return 1;
}
