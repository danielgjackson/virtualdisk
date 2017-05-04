// VirtualDisk Main Demo Code
// Dan Jackson, 2013

// Includes
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../virtualdisk/virtualdisk.h"
#include "../virtualdisk/virtualdiskio.h"
#include "../fatfs/ff.h"

// File-system state
static FATFS fs;
static virtualdisk_partition_t partition;
virtualdisk_t virtualdisk;

// For testing non-standard sectors
#if VIRTUALDISK_DEFAULT_SECTOR_SIZE > _MAX_SS
    #error "_MAX_SS must be at least VIRTUALDISK_DEFAULT_SECTOR_SIZE"
#endif

// Generate the specified number of sectors of a file's contents
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

// Call to retrieve information about the specified file entry
char VirtualDiskFileInfo(virtualdisk_fileinfo_t *fileInfo)
{
    static char filename[13] = {0};

    // Default return values
    fileInfo->filename = NULL;
    fileInfo->attributes = 0;
    fileInfo->size = 0;
    fileInfo->contents = NULL;
    fileInfo->created = VIRTUALDISK_DATETIME_MIN;
    fileInfo->modified = VIRTUALDISK_DATETIME_MIN;
    fileInfo->accessed = VIRTUALDISK_DATETIME_MIN;
    fileInfo->reference = NULL;

    // Volume label
#if 0
    if (fileInfo->id == 0)
    {
        fileInfo->filename = "VIRTUAL";
        fileInfo->attributes = VIRTUALDISK_ATTRIB_VOLUME;
        fileInfo->size = 0;
        return 1;
    }
    else
#endif

    if (fileInfo->id <= 4)
    {
        sprintf(filename, "TEST%04X.TXT", fileInfo->id);
        fileInfo->filename = filename;
        fileInfo->attributes = VIRTUALDISK_ATTRIB_ARCHIVE;
        fileInfo->size = 3 * 512;
        fileInfo->contents = VirtualDiskFileContents;
        return 1;
    }

    return 0;       // No more files
}


void WriteLocalFileFromFile(const char *destFilename, const char *sourceFilename)
{
    FIL sfp;
    FRESULT res;
    res = f_open(&sfp, sourceFilename, FA_OPEN_EXISTING | FA_READ);
    if (!res)
    {
        FILE *dfp = fopen(destFilename, "wb");
        if (dfp != NULL)
        {
            unsigned long total = 0;
            while (!f_eof(&sfp))
            {
                static char *buffer[256];
                unsigned int len;
                res = f_read(&sfp, buffer, sizeof(buffer), &len);
                if (res || len == 0) { break; }
                total += len;
                fwrite(buffer, 1, len, dfp);
            }
            printf(">>> [%ld bytes]\n", total);
            fclose(dfp);
        }
        else
        {
            printf("[Problem opening dest file %s]\n", destFilename);
        }
        f_close(&sfp);
    }
    else
    {
        printf("[Problem opening source file %s]\n", sourceFilename);
    }
}

void PrintFile(const char *filename)
{
    FIL fp;
    FRESULT res;
    res = f_open(&fp, filename, FA_OPEN_EXISTING | FA_READ);
    if (!res)
    {
        while (!f_eof(&fp))
        {
            static char buffer[512 + 1];
            unsigned int len;
            res = f_read(&fp, buffer, sizeof(buffer) - 1, &len);
            buffer[len] = 0;
            if (res || len == 0) { break; }
buffer[72] = '>'; buffer[73] = '>'; buffer[74] = '>'; buffer[75] = '\0';
            printf(">>> %s\n", buffer);
        }
        f_close(&fp);
    }
    else
    {
        printf("[Problem opening file for printing %s]\n", filename);
    }
}



int main(void)
{
    FRESULT res;

    // Create virtual disk
    VirtualDiskInit(&virtualdisk, VIRTUALDISK_DEFAULT_SECTOR_SIZE);

    // Test different partition types (FAT12/FAT16/FAT32, with different cluster sizes, etc.)
//    VirtualDiskAddPartition(&virtualdisk, &partition, VirtualDiskFileInfo, VIRTUALDISK_DEFAULT_SECTORS_PER_CLUSTER, VIRTUALDISK_DEFAULT_COUNT_DATA_CLUSTERS, VIRTUALDISK_DEFAULT_ROOT_DIR_ENTRIES);
//    VirtualDiskAddPartition(&virtualdisk, &partition, VirtualDiskFileInfo, VIRTUALDISK_DEFAULT_SECTORS_PER_CLUSTER, VIRTUALDISK_DEFAULT_COUNT_DATA_CLUSTERS, 16);
VirtualDiskAddPartition(&virtualdisk, &partition, VirtualDiskFileInfo, 1, 30, 16);
//VirtualDiskAddPartition(&virtualdisk, &partition, VirtualDiskFileInfo, 0x40, 65500, 63 * 1024);

	// Map FatFs drive 0 to our virtual disk
	VirtualDiskIOSet(0, &virtualdisk);

    // Initialize the file-system for reading
    if ((res = f_mount(0, &fs)) != FR_OK)
    {
        printf("[Problem mounting drive %d]\n", res);
    }

    // Disk free
    if (0)
    {
        DWORD clusters = 0;
        FATFS *fsp = &fs;
        if ((res = f_getfree("", &clusters, &fsp)) == FR_OK)
        {
            printf("[Free clusters: %lu]\n", clusters);
        }
        else
        {
            printf("[Problem getting free clusters]\n");
        }
    }

    // Volume label
    {
        char label[12] = "";
        unsigned long id = 0;
        if ((res = f_getlabel("", label, &id)) == FR_OK)
        {
            printf("[Label: '%s', Id: %lu]\n", label, id);
        }
        else
        {
            printf("[Problem getting label]\n");
        }
    }

    // Open directory
    {
        DIR dj;
        if ((res = f_opendir(&dj,"")) != FR_OK)
        {
            printf("[Problem opening directory]\n");
        }
        else
        {
            FILINFO fno;
            for (;;)
            {
                if ((res = f_readdir(&dj, &fno)) != FR_OK) { printf("[Problem reading directory]\n"); break; }
                if (fno.fname[0] == '\0') { break; }
                printf("[File: '%s', %lu bytes, Date: %x, Time: %x, Attrib: %x]\n", fno.fname, fno.fsize, fno.fdate, fno.ftime, fno.fattrib);
            }
        }
    }

    // File status
    {
        FILINFO fno = {0};
        if ((res = f_stat("test0001.txt", &fno)) == FR_OK)
        {
            printf("[STAT: File: '%s', %lu bytes, Date: %x, Time: %x, Attrib: %x]\n", fno.fname, fno.fsize, fno.fdate, fno.ftime, fno.fattrib);
        }
        else
        {
            printf("[Problem getting file status]\n");
        }
    }

/// ...

//    WriteLocalFileFromFile("test.txt", "test.txt");
    PrintFile("test0001.txt");
//    PrintFile("test000f.txt");

#if 1
    {
        unsigned char buffer[64 * 1024];
        FILE *dfp;
        memset(buffer, 0xcc, sizeof(buffer));
        VirtualDiskReadSectors(&virtualdisk, 0, (unsigned short)(sizeof(buffer) / VirtualDiskSectorSize(&virtualdisk)), buffer);
        // Write out
        if ((dfp = fopen("dump.bin", "wb")) != NULL)
        {
            fwrite(buffer, 1, sizeof(buffer), dfp);
            fclose(dfp);
        }
    }
#endif

#if defined(_WIN32) && defined(_DEBUG)
    getchar();
#endif

    return 0;
}
