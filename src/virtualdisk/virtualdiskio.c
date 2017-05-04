// Virtual Disk/File System - FatFs Bridge
// Dan Jackson, 2013

#include "virtualdisk/virtualdisk.h"
#include "fatfs/diskio.h"
#include "ffconf.h"				// _VOLUMES

// Drive-to-virtual disk mapping
static virtualdisk_t *virtualdisk[_VOLUMES] = { 0 };

// Configure the Virtual Disk for the specified drive
void VirtualDiskIOSet(unsigned char drive, virtualdisk_t *vd)
{
	virtualdisk[drive] = vd;
	return;
}

DSTATUS disk_initialize(BYTE drive)
{
	virtualdisk_t *state = virtualdisk[drive];
	state = &virtualdisk;
    return disk_status(drive);
}

DSTATUS disk_status(BYTE drive)
{
	virtualdisk_t *state = virtualdisk[drive];
	unsigned char flags = 0;
    if (drive != 0 || state == 0 || !state->initialized) { flags |= STA_NOINIT; }

    flags |= STA_PROTECT;   // Read-only, cannot write

    return flags;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, DWORD sector, BYTE count)
{
	virtualdisk_t *state = virtualdisk[drive];
	if (drive != 0) { return RES_PARERR; }
    if (buffer == 0) { return RES_PARERR; }
    if (state == 0 || !state->initialized) { return RES_NOTRDY; }
    if (!VirtualDiskReadSectors(state, sector, count, buffer))
    {
        return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, DWORD sector, BYTE count)
{
	virtualdisk_t *state = virtualdisk[drive];
	if (drive != 0) { return RES_PARERR; }
    if (buffer == 0) { return RES_PARERR; }
    if (state == 0 || !state->initialized) { return RES_NOTRDY; }

    return RES_WRPRT;       // Read-only, cannot write

    //return RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
	virtualdisk_t *state = virtualdisk[drive];
	if (drive != 0) { return RES_PARERR; }
    if (buffer == 0) { return RES_PARERR; }
    if (state == 0 || !state->initialized) { return RES_NOTRDY; }

    switch (command)
    {
        case CTRL_SYNC:
            // Ignore (no writes)
            return RES_OK;

        case GET_SECTOR_COUNT:
            *((unsigned long *)buffer) = VirtualDiskSectorCount(state);
            return RES_OK;

        case GET_SECTOR_SIZE:
            *((unsigned short *)buffer) = VirtualDiskSectorSize(state);
            return RES_OK;

        case GET_BLOCK_SIZE:
            // Assume block size is 1 sector
            *((unsigned long *)buffer) = 1 * VirtualDiskSectorSize(state);
            return RES_OK;

        case CTRL_ERASE_SECTOR:
            // Ignore (no writes)
            return RES_OK;
    }

    return RES_PARERR;
}

unsigned long get_fattime(void)
{
    return 0;
}
