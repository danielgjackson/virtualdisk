// Virtual Disk/File System
// Dan Jackson, 2013

#ifndef VIRTUALDISKIO_H
#define VIRTUALDISKIO_H

//#include "fatfs/diskio.h"

#include "virtualdisk.h"


// Configure the Virtual Disk for the specified drive
void VirtualDiskIOSet(unsigned char drive, virtualdisk_t *virtualdisk);


// Disk read/write/ioctl result values (compatible with FatFs)
#define VIRTUALDISKIO_RESULT_OK                     0       // Successful
#define VIRTUALDISKIO_RESULT_ERROR_READ_WRITE       1       // Read/write error
#define VIRTUALDISKIO_RESULT_ERROR_WRITE_PROTECT    2       // Write protected
#define VIRTUALDISKIO_RESULT_ERROR_NOT_READY        3       // Not ready
#define VIRTUALDISKIO_RESULT_ERROR_PARAMETER        4       // Invalid parameter

// Disk control functions (compatible with FatFs)
unsigned char disk_initialize(unsigned char drive);
unsigned char disk_status(unsigned char drive);
int disk_read(unsigned char drive, unsigned char *buffer, unsigned long sector, unsigned char count);
int disk_write(unsigned char drive, const unsigned char *buffer, unsigned long sector, unsigned char count);
int disk_ioctl(unsigned char drive, unsigned char command, void *buffer);

// Disk status bits (compatible with FatFs)
#define VIRTUALDISKIO_STATUS_NOT_INIT           0x01    // Drive not initialized
#define VIRTUALDISKIO_STATUS_NO_DISK            0x02    // No disk in drive
#define VIRTUALDISKIO_STATUS_WRITE_PROTECTED    0x04    // Write protected

// IOCTL commands (compatible with FatFs)
#define VIRTUALDISKIO_IOCTL_SYNC                0       // Flush writes
#define VIRTUALDISKIO_IOCTL_GET_SECTOR_COUNT    1       // Media size
#define VIRTUALDISKIO_IOCTL_GET_SECTOR_SIZE     2       // Sector size
#define VIRTUALDISKIO_IOCTL_GET_BLOCK_SIZE      3       // Erase block size
//#define VIRTUALDISKIO_IOCTL_ERASE_SECTOR        4       // Erase a sector


#endif
