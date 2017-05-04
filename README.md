# Virtual Disk


## Overview

This virtual disk driver emulates a drive with FAT12/FAT16/FAT32 partitions containing virtual files and file contents. 
Sectors are produced as needed -- no sectors are stored in memory, and nothing but the essential information is cached (importantly, no "per file" overhead). 
A "generator" is used to create sectors as they are requested (e.g. FAT table, directory contents, each file's contents). 
The "generator" is cached, so this performs well for normal, linear reads from the file-system (incrementally moving to the next file is also a constant-time operation). 
The user supplies a function that returns information about each file in the root directory (including a function that will generate the file contents). 

Sub-directories are not yet supported (when they are, a more elegant fix for FAT32 root directory entries should also be added too). 
(However, tens of thousands of files can be put into the root directory.)

Test code is included that uses FatFs to read files from the virtual disk. 


## Usage

```c
// VirtualDisk API
#include "virtualdisk.h"


// State storage
virtualdisk_t virtualdisk;
virtualdisk_partition_t partition;


// Call-back 'generator' function for the file contents
unsigned short VirtualDiskFileContents(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer)
{
    virtualdisk_fileinfo_t *fileInfo = (virtualdisk_fileinfo_t *)reference;
    unsigned short sectorSize = VirtualDiskSectorSize(&virtualdisk);

    // Generate a sector with the current sector number written in it
    sprintf((char *)buffer, "[#%d=%s:%08ld]", fileInfo->id, fileInfo->filename, sector);
    memset(buffer + strlen((char *)buffer), '.', sectorSize - strlen((char *)buffer));
	buffer[sectorSize - 2] = '\r';
	buffer[sectorSize - 1] = '\n';

    return 1;   // Generated one sector
}


// Call-back function to get information about each file in the root directory
char VirtualDiskFileInfo(virtualdisk_fileinfo_t *fileInfo)
{
    fileInfo->filename = filename;
    fileInfo->attributes = VIRTUALDISK_ATTRIB_VOLUME;
    fileInfo->created = VIRTUALDISK_DATETIME_MIN;
    fileInfo->modified = VIRTUALDISK_DATETIME_MIN;
    fileInfo->accessed = VIRTUALDISK_DATETIME_MIN;

    switch (fileInfo->id)
    {
        case 0:
            fileInfo->filename = "VIRTUAL";
            fileInfo->attributes = VIRTUALDISK_ATTRIB_VOLUME;
            fileInfo->size = 0;
            return 1;
        case 1:
            fileInfo->filename = "TEST.TXT";
            fileInfo->attributes = VIRTUALDISK_ATTRIB_ARCHIVE;
            fileInfo->size = 1024;
            fileInfo->contents = VirtualDiskFileContents;
            return 1;
        default:
            return 0;       // No more files
    }
}


// Demonstration
void main(void)
{
    // Create the virtual disk
    VirtualDiskInit(&virtualdisk, VIRTUALDISK_DEFAULT_SECTOR_SIZE);

    // Add a FAT16 partition
    VirtualDiskAddPartition(&virtualdisk, &partition, VirtualDiskFileInfo, 1, 60, 16);

    // Test
    {
        unsigned short sectorSize = VirtualDiskSectorSize(&virtualdisk);
        unsigned long sectorCount = VirtualDiskSectorCount(&virtualdisk);
        unsigned char buffer[60 * VIRTUALDISK_DEFAULT_SECTOR_SIZE];
        VirtualDiskReadSectors(&virtualdisk, 0, sizeof(buffer) / sectorSize, buffer);
    }
}
```
