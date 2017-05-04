// Virtual Disk/File System
// Dan Jackson, 2013

// NOTE: See README.md for explanation of use.

#include <stdlib.h>
#include <string.h>

#include "virtualdisk.h"

// Debug trace
//#define VIRTUALDISK_DEBUG

// Enable FAT32 (but in a hacky way) - hopefully can be done more nicely once subdirectories are supported
#define VIRTUALDISK_HACK_FAT32


// Little-endian word writing macros
#define SET_DWORD(_p, _ov) { unsigned long _v = (_ov); *((_p)+0) = (unsigned char)((_v)); *((_p)+1) = (unsigned char)((_v) >> 8); *((_p)+2) = (unsigned char)((_v) >> 16); *((_p)+3) = (unsigned char)((_v) >> 24); }
#define SET_WORD(_p, _ov)  { unsigned short _v = (_ov); *((_p)+0) = (unsigned char)((_v)); *((_p)+1) = (unsigned char)((_v) >> 8); }
#define SET_CHS(_p, _c, _h, _s) { *((_p)+0) = (unsigned char)(_h); *((_p)+1) = (unsigned char)(((_c) >> 8) | (_s)); *((_p)+2) = (unsigned char)(_c);  } // Cylinder-head-sector: hhhhhhhh ccssssss cccccccc, CHS=(1023,254,63)
#define SET_DATETIME_FAT_DATE(_p, _od) { unsigned long _d = (_od); *((_p)+0) = (unsigned char)((_d) >> 17); *((_p)+1) = (unsigned char)((_d) >> 25) + 40; }    // Write [YYYYYYMM MMDDDDDh hhhhmmmm mmssssss] as FAT Date [15-9=Y, 8-5=M1, 4-0=D1]
#define SET_DATETIME_FAT_TIME(_p, _od) { unsigned long _d = (_od); *((_p)+0) = (unsigned char)((_d) >>  1); *((_p)+1) = (unsigned char)((_d) >>  9); }         // Write [YYYYYYMM MMDDDDDh hhhhmmmm mmssssss] as FAT Time [15-11=H, 10-5=M, 4-0=S/2]


// (Private) Seek a file enumerator to the specified id
char VirtualDiskFileEnumeratorSeekId(virtualdisk_file_enumerator_t *fileEnumerator, int id)
{
    // If need to reset...
    if (id < 0 || id < fileEnumerator->fileInfo.id)
    {
#ifdef VIRTUALDISK_DEBUG
        printf("!!! ENUMERATOR-RESET\n");
#endif
        // Get first file information
        fileEnumerator->fileInfo.id = 0;
        fileEnumerator->firstCluster = 2;       // First FAT cluster is 2
#ifdef VIRTUALDISK_HACK_FAT32
        if (fileEnumerator->partition->fatType == VIRTUALDISK_FAT32)
        {
            // HACK: Adjust to point to cluster after FAT32 root directory cluster chain
            fileEnumerator->firstCluster += (fileEnumerator->partition->sectorsRootDir / fileEnumerator->partition->sectorsPerCluster);
        }
#endif
        fileEnumerator->hasFile = fileEnumerator->fileInfoCallback(&fileEnumerator->fileInfo);
        fileEnumerator->numClusters = (fileEnumerator->fileInfo.size + (fileEnumerator->partition->sectorsPerCluster * fileEnumerator->partition->disk->sectorSize - 1)) / fileEnumerator->partition->sectorsPerCluster / fileEnumerator->partition->disk->sectorSize;
    }

    // Advance to required index
    while (fileEnumerator->fileInfo.id < id)
    {
        // Short exit if no more files
        if (!fileEnumerator->hasFile) { return 0; }

        // Get next information
        fileEnumerator->fileInfo.id++;
        fileEnumerator->firstCluster += fileEnumerator->numClusters;
        fileEnumerator->hasFile = fileEnumerator->fileInfoCallback(&fileEnumerator->fileInfo);
        fileEnumerator->numClusters = (fileEnumerator->fileInfo.size + (fileEnumerator->partition->sectorsPerCluster * fileEnumerator->partition->disk->sectorSize - 1)) / fileEnumerator->partition->sectorsPerCluster / fileEnumerator->partition->disk->sectorSize;
    }

    return fileEnumerator->hasFile;
}


// (Private) Seek a file enumerator to the next file
char VirtualDiskFileEnumeratorNext(virtualdisk_file_enumerator_t *fileEnumerator)
{
    return VirtualDiskFileEnumeratorSeekId(fileEnumerator, fileEnumerator->fileInfo.id + 1);
}


// (Private) Seek a file enumerator to the one covering the specified cluster
char VirtualDiskFileEnumeratorSeekCluster(virtualdisk_file_enumerator_t *fileEnumerator, unsigned long cluster)
{
    // If we're looking for a cluster before the current one, reset
    if (cluster < fileEnumerator->firstCluster)
    {
        if (!VirtualDiskFileEnumeratorSeekId(fileEnumerator, 0))
        {
            return 0;       // No files - cluster not found
        }
    }

    if (!fileEnumerator->hasFile) { return 0; }

    // Advance to look for the required cluster
    while (cluster >= fileEnumerator->firstCluster + fileEnumerator->numClusters || fileEnumerator->numClusters == 0)
    {
        if (!VirtualDiskFileEnumeratorNext(fileEnumerator))
        {
            return 0;       // No more files - cluster not found
        }
    }

    return 1;
}


// (Private) Initialize a file enumerator
char VirtualDiskFileEnumeratorInit(virtualdisk_file_enumerator_t *fileEnumerator, virtualdisk_partition_t *partition, VirtualDiskFileInfoCallback fileInfoCallback)
{
    fileEnumerator->partition = partition;
    fileEnumerator->firstCluster = 2;       // First FAT cluster is 2
#ifdef VIRTUALDISK_HACK_FAT32
    if (fileEnumerator->partition->fatType == VIRTUALDISK_FAT32)
    {
        // HACK: Adjust to point to cluster after FAT32 root directory cluster chain
        fileEnumerator->firstCluster += (fileEnumerator->partition->sectorsRootDir / fileEnumerator->partition->sectorsPerCluster);
    }
#endif

    fileEnumerator->fileInfoCallback = fileInfoCallback;

    VirtualDiskFileEnumeratorSeekId(fileEnumerator, -1);

    return fileEnumerator->hasFile;
}


char VirtualDiskInit(virtualdisk_t *disk, unsigned short sectorSize)
{
    // Check sector size must be at least 512 bytes and be a power of 2
    if (sectorSize < 512 || (sectorSize & (sectorSize - 1)) != 0)
    {
        return 0;   // Sector size invalid
    }
    disk->sectorSize = sectorSize;

    // Start with no partitions
    disk->numPartitions = 0;

    // Number of sectors on the virtual drive -- just the MBR to begin with
    disk->sectorCount = 1;

    // Set as initialized
    disk->initialized = 1;

    return 1;
}

char VirtualDiskAddPartition(virtualdisk_t *disk, virtualdisk_partition_t *partition, VirtualDiskFileInfoCallback fileInfoCallback, unsigned char sectorsPerCluster, unsigned long countDataClusters, unsigned short rootDirEntries)
{
    // Check we have space to add the partition
    if (disk->numPartitions >= VIRTUALDISK_MAX_PARTITIONS) { return 0; }    // Too many partitions

    // Check values
    if (sectorsPerCluster < 1 || (sectorsPerCluster & (sectorsPerCluster - 1)) != 0 || sectorsPerCluster > 0x80) { return 0; }         // ERROR: Invalid number of sectors per cluster
    //if (countDataClusters < 1) { return 0; }        // ERROR: Invalid number of data clusters

    // Set partition owner to be the disk
    partition->disk = disk;

    // Initialize partition
    {
        partition->fileInfoCallback = fileInfoCallback;
        partition->binaryId = 0ul;

        // Virtual disk parameters
        partition->sectorsPerCluster = sectorsPerCluster;   // e.g. 0x40 -- 64 * sector_size = 32Kb clusters
        partition->countDataClusters = countDataClusters;   // e.g. 64768.  Count of the number of data clusters (max cluster entries will be count+2).  FAT16 drive if this is between 4085 and 65524, FAT12 below, and FAT32 above.
        partition->numFat = VIRTUALDISK_DEFAULT_NUM_FAT;    // Number of FAT tables (usually 2, 1 is also supported).
        partition->rootDirEntries = rootDirEntries;         // Number of root directory entries (ignored on FAT32)

        // Calculate FAT type
        if (partition->countDataClusters < 4085) { partition->fatType = VIRTUALDISK_FAT12; }
        else if(partition->countDataClusters < 65525) { partition->fatType = VIRTUALDISK_FAT16; }
        else { partition->fatType = VIRTUALDISK_FAT32; }

        // Calculate region lengths
        partition->sectorsReserved = (partition->fatType == VIRTUALDISK_FAT32) ? 32 : 1;        // Number of reserved sectors on the partition before the FAT, including the boot sector (FAT12/FAT16 at least 1, FAT32 commonly 32)
        partition->sectorsData = partition->countDataClusters * partition->sectorsPerCluster;   // Number of sectors in the data region

        // FAT0 region length (depends on cluster entry size)
        partition->sectorsFat0 = (partition->countDataClusters + 2);
        if (partition->fatType == VIRTUALDISK_FAT12) { partition->sectorsFat0 += (partition->sectorsFat0 >> 1); }
        else if (partition->fatType == VIRTUALDISK_FAT16) { partition->sectorsFat0 <<= 1; }
        else if (partition->fatType == VIRTUALDISK_FAT32) { partition->sectorsFat0 <<= 2; }
        partition->sectorsFat0 = (partition->sectorsFat0 + (partition->disk->sectorSize - 1)) / partition->disk->sectorSize;

        // Number of sectors for root directory
        if (partition->fatType == VIRTUALDISK_FAT32)
        {
            // None really on FAT32 (root directory stored in the data area with a cluster chain)
            partition->sectorsRootDir = 0;
#ifdef VIRTUALDISK_HACK_FAT32
            // HACK: Until subdirectories are supported, use the root directory code from FAT12/16 for FAT32 - round up to to a whole cluster cluster.
            partition->sectorsRootDir = (((unsigned long)(partition->rootDirEntries * 32) + (partition->disk->sectorSize * partition->sectorsPerCluster - 1)) / partition->disk->sectorSize / partition->sectorsPerCluster) * partition->sectorsPerCluster;
#endif
        }
        else
        {
            // Calculate number of sectors of root directory
            partition->sectorsRootDir = ((partition->rootDirEntries * 32) + (partition->disk->sectorSize - 1)) / partition->disk->sectorSize;
        }

        // Calculate region addresses
        partition->regionData = (partition->sectorsReserved + (partition->sectorsFat0 * partition->numFat) + partition->sectorsRootDir);

        // The virtual partition start and length
        partition->partitionStartSector = 0;        // The number of padding sectors to add before this partition starts
        partition->partitionSizeSectors = partition->regionData + partition->sectorsData;

        // Create a file enumerator
        VirtualDiskFileEnumeratorInit(&partition->fileEnumerator, partition, partition->fileInfoCallback);
    }

    // Append this partition to the disk
    partition->partitionStartSector += disk->sectorCount;
    disk->sectorCount = partition->partitionStartSector + partition->partitionSizeSectors;

    // Add partition to the disk
    disk->partitions[disk->numPartitions] = partition;
    disk->numPartitions++;

    return 1;
}


// (Private) Generate a MBR for a disk
unsigned short VirtualDiskGenerateMBR(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer)
{
    virtualdisk_t *disk = (virtualdisk_t *)reference;
    int i;
    if (count <= 0) { return 0; }

    // Start with a blank
    memset(buffer, 0, disk->sectorSize);

    // Only the first sector on a disk is the MBR (other blank ones could exist before the first partition)
    if (sector == 0)
    {
        SET_DWORD(buffer + 0x01B8, 0xF58B16F5ul);                               // @0x01B8 Disk signature
    
        // @0x01BE Table of primary partitions (16 bytes/entry x 4 entries)
        for (i = 0; i < 4; i++)
        {
            unsigned char *p = buffer + 0x01BE + (i * 16);

            if (i < disk->numPartitions)
            {
                p[0] = 0x80;                                                    // Status - 0x80 (bootable), 0x00 (not bootable), other (error)
                SET_CHS(p + 1, 0, 1, 1);                                        // Cylinder-head-sector address of last sector in partition,  hhhhhhhh ccssssss cccccccc CHS=(0,1,1)
                // Partition type: 0x01 = FAT12; 0x04 = FAT16 <32MB; 0x06 = FAT16 32MB+; 0x0C = FAT32 (FAT32X LBA-access); (0x07 = exFAT)
                if (disk->partitions[i]->fatType == VIRTUALDISK_FAT32) { p[4] = 0x0c; }
                else if (disk->partitions[i]->fatType == VIRTUALDISK_FAT12) { p[4] = 0x01; }
                else if (disk->partitions[i]->partitionSizeSectors <= 0xffff) { p[4] = 0x04; }
                else { p[4] = 0x06; }
                SET_CHS(p + 5, 0, 1, 1);  //SET_CHS(p + 5, 1023, 254, 63)       // Cylinder-head-sector address of last sector in partition,  hhhhhhhh ccssssss cccccccc CHS=(1023,254,63)
                SET_DWORD(p + 8, disk->partitions[i]->partitionStartSector);    // Logical block address of first sector in partition
                SET_DWORD(p + 12, disk->partitions[i]->partitionSizeSectors);   // Length of partition in sectors (512MB 0x100000 sectors)
            }
        }
        SET_WORD(buffer + 0x1fe, 0xaa55);                                       // @0x01FE MBR signature
    }

    return 1;
}


// (Private) Generate a sector in the reserved area (the first of which will be a boot sector)
unsigned short VirtualDiskPartitionGenerateReserved(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer)
{
    virtualdisk_partition_t *partition = (virtualdisk_partition_t *)reference;

    // Reserved sectors are mostly blank
    memset(buffer, 0, partition->disk->sectorSize);

    // First sector in the reserved (first) area of a volume is a boot sector (also 6th sector of FAT32 is the backup boot sector)
    if (sector == 0 || (partition->fatType == VIRTUALDISK_FAT32 && sector == 6))
    {
        buffer[0] = 0xeb; buffer[1] = 0x3c; buffer[2] = 0x90;           // @0x0000 Jump instruction
        memcpy(buffer + 3, "MSDOS5.0", 8);                              // @0x0003 OEM Name "MSDOS5.0"
        SET_WORD(buffer + 11, partition->disk->sectorSize);             // @0x000b Bytes per sector
        buffer[13] = partition->sectorsPerCluster;                      // @0x000d Sectors per cluster
        SET_WORD(buffer + 14, partition->sectorsReserved);              // @0x000e Reserved sector count (FAT12/FAT16 at least 1, FAT32 commonly 32)
        buffer[16] = partition->numFat;                                 // @0x0010 Number of FATs (1)
        SET_WORD(buffer + 17, (partition->fatType == VIRTUALDISK_FAT32) ? 0x0000 : ((unsigned short)partition->sectorsRootDir * (partition->disk->sectorSize / 32)));  // @0x0011 (FAT12/FAT16) Max number of root directory entries - 32-bytes each entry, 16 files allowed per sector
        SET_WORD(buffer + 19, (partition->partitionSizeSectors <= 0xffff && partition->fatType != VIRTUALDISK_FAT32) ? (unsigned short)partition->partitionSizeSectors : 0x0000);  // @0x0013 Total sectors (0 = use 4-byte number later)
        buffer[21] = 0xF8;                                              // @0x0015 Media Descriptor (0xF8-fixed, 0xF0-removable)
        SET_WORD(buffer + 22, (partition->fatType != VIRTUALDISK_FAT32) ? (unsigned short)partition->sectorsFat0 : 0x0000);    // @0x0016 Sectors per FAT (FAT12/FAT16)
        SET_WORD(buffer + 24, 0x3f);                                    // @0x0018 Sectors per track
        SET_WORD(buffer + 26, 0xff);                                    // @0x001a Number of heads
        SET_DWORD(buffer + 28, partition->partitionStartSector);        // @0x001c Hidden sectors (on disk before boot sector) // HACK: BPB_HiddSec may need to know about *all* other sectors on the drive before this partition (but think it may be largely unused)
        SET_DWORD(buffer + 32, (partition->partitionSizeSectors > 0xffff || partition->fatType == VIRTUALDISK_FAT32) ? partition->partitionSizeSectors : 0); // @0x0020 Total sectors
        if (partition->fatType == VIRTUALDISK_FAT32)
        {
            SET_DWORD(buffer + 36, partition->sectorsFat0);             // @36 Sectors in FAT0 (FAT32)
            SET_WORD(buffer + 40, 0x0000);                              // @40 ExtFlags (b0-3 = active FAT, b7 = only use active FAT, otherwise mirroring to all FATs enabled)
            SET_WORD(buffer + 42, 0x0000);                              // @42 File-system version (0.0)
            SET_DWORD(buffer + 44, 2);                                  // @44 Root directory cluster
            SET_WORD(buffer + 48, 1);                                   // @48 FSINFO sector number within reserved area (usually 1)
            SET_WORD(buffer + 50, 0);                                   // @50 Non-zero indicates backup boot record sector number within reserved area (usually 6, 0 = none)
            memset(buffer + 52, 0, 12);                                 // @52 Reserved (12 bytes)
            buffer[64] = 0x00;                                          // @64 Physical drive number (0x80 = fixed disk, 0x00 = removable)
            buffer[65] = 0x00;                                          // @65 Reserved (Current Head), bit-0 = dirty, bit-1 = surface scan
            buffer[66] = 0x29;                                          // @66 Signature (=0x29)
            SET_DWORD(buffer + 67, partition->binaryId);                // @67 Binary ID (4 bytes) @0x27 @39 {0x01, 0x00, 0x00, 0x00}
            memcpy(buffer + 71, "NO NAME    ", 11);                     // @71 FAT volume label (11 bytes) @0x2B @43 {'N','O',' ','N','A','M','E',' ',' ',' ',' ',}
            memcpy(buffer + 82, "FAT32   ", 8);                         // @82 FAT system (8 bytes)
        }
        else
        {
            buffer[36] = 0x00;                                          // @0x0024 Physical drive number (0x80 = fixed disk, 0x00 = removable)
            buffer[37] = 0x00;                                          // @0x0025 Reserved (Current Head), bit-0 = dirty, bit-1 = surface scan
            buffer[38] = 0x29;                                          // @0x0026 Signature (=0x29)
            SET_DWORD(buffer + 39, partition->binaryId);                // @0x0027 Binary ID (4 bytes) @0x27 @39 {0x01, 0x00, 0x00, 0x00}
            memcpy(buffer + 43, "NO NAME    ", 11);                     // @0x002b FAT volume label (11 bytes) @0x2B @43 {'N','O',' ','N','A','M','E',' ',' ',' ',' ',}
            memcpy(buffer + 54, (partition->fatType == VIRTUALDISK_FAT12) ? "FAT12   " : "FAT16   ", 8);     // @0x0036 FAT system (8 bytes)
        }
        SET_WORD(buffer + 0x1fe, 0xaa55);                               // @0x01FE Signature
    }
    else if (partition->fatType == VIRTUALDISK_FAT32 && (sector == 1 || sector == 7))
    {
        // FAT32 FSInfo sector at sector 1 (or 7 for backup)
        SET_DWORD(buffer + 0, 0x41615252ul);                            // @0 Lead signature for FSInfo sector
        memset(buffer + 4, 0, 480);                                     // @4 480 reserved zero bytes
        SET_DWORD(buffer + 484, 0x61417272ul);                          // @484 Signature for FSInfo sector
        SET_DWORD(buffer + 488, 0xfffffffful);                          // @488 Last known free cluster count (0xffffffff is unknown, could set to 0 on full disk?)
        SET_DWORD(buffer + 492, 0xfffffffful);                          // @492 Hint for cluster to start looking for free clusters (0xffffffff is no hint, first is 2)
        memset(buffer + 496, 0, 12);                                    // @496 12 reserved zero bytes
        SET_DWORD(buffer + 508, 0xaa550000ul);                          // @508 4-byte signature (ends 0x55,0xaa as all others)
    }
    else if (partition->fatType == VIRTUALDISK_FAT32 && (sector == 2 || sector == 8))
    {
        // "Third boot sector" common on FAT32
        memset(buffer, 0, 510);                                         // Zero bytes
        SET_WORD(buffer + 0x1fe, 0xaa55);                               // @0x01FE Signature
    }

    return 1;
}


// (Private) Generate a sector from the FAT
unsigned short VirtualDiskPartitionGenerateFAT(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer)
{
    virtualdisk_partition_t *partition = (virtualdisk_partition_t *)reference;
    // Small FAT12 example:
    //     0xF8,0xFF,   // FAT12 entry 0-1: Copy of the media descriptor 0xFF8
    //     0xFF,        // FAT12 entry 1:   EOC 0xFFF
    //     0xFF,0x0F    // FAT12 entry 2:   0xFFF
    //
    // Small FAT16 example:
    //     0xF8, 0xFF,	// FAT16 entry 0: Copy of the media descriptor 0xF8, remaining 8-bits 0xff
    //     0xFF, 0xFF,	// FAT16 entry 1: End of cluster chain marker (bit 15 = last shutdown was clean, bit 14 = no disk I/O errors were detected)
    //     0x03, 0x00,	// FAT16 entry 2: (First file) points to next entry in the cluster chain
    //     0xFF, 0xFF,	// FAT16 entry 3: End of cluster chain marker (first file is two clusters long)
    //     0xF7, 0xFF,	// FAT16 entry 4: Cluster marked as bad (OS won't try to use it)
	unsigned long entry = 0;
	unsigned long fatOffset;
	unsigned long value;
	unsigned char *p = (unsigned char *)buffer;
	unsigned int i;
    unsigned char part = 0;     // For FAT12 fragments

    virtualdisk_file_enumerator_t *fileEnumerator = &partition->fileEnumerator;

	// byte offset within FAT table
	fatOffset = sector;
    if (partition->numFat > 1 && fatOffset > partition->sectorsFat0) { fatOffset %= partition->sectorsFat0; }   // Mirror
    fatOffset *= partition->disk->sectorSize;

    if (partition->fatType == VIRTUALDISK_FAT12)
    {
		// Each entry is 12 bits, write a 3 bytes (2 entries = 24-bits) each iteration
		// Example if first file is 3kB with 512-byte clusters, FAT12: ff8 fff 003 004 005 006 007 fff
        part = (fatOffset * 2) % 3; // which byte of the two-entry triplets: aa ba bb ?
        entry = (fatOffset << 1) + (fatOffset >> 1);    // FAT12
    }
    else if (partition->fatType == VIRTUALDISK_FAT16)
    {
	    // Using FAT16, each entry is 16 bits
		entry = (fatOffset >> 1);
    }
    else if (partition->fatType == VIRTUALDISK_FAT32)
    {
	    // Using FAT32, each entry is 32 bits
		entry = (fatOffset >> 2);
    }

	for (i = 0; i < partition->disk->sectorSize; )
	{
        if      (entry == 0)           { value = 0x0ffffff8; }  	// Entry 0: Copy of the media descriptor (0xf8), remaining 8-bits set (0xff)
		else if (entry == 1)           { value = 0x0fffffff; }  	// Entry 1: End of cluster chain marker (bit 15 = last shutdown was clean, bit 14 = no disk I/O errors were detected)
#ifdef VIRTUALDISK_HACK_FAT32
        else if (fileEnumerator->partition->fatType == VIRTUALDISK_FAT32 && entry < 2 + (fileEnumerator->partition->sectorsRootDir / fileEnumerator->partition->sectorsPerCluster))
        {
            // HACK: FAT32 root directory cluster chain
            unsigned long end = 2 + (fileEnumerator->partition->sectorsRootDir / fileEnumerator->partition->sectorsPerCluster);
            if (entry < end - 1) { value = (entry + 1); }               // Entry 2...(end-1): Next cluster of file
            else { value = 0x0fffffff; }                            // Entry (end): End of chain (0xffff)
        }
#endif
        else if (VirtualDiskFileEnumeratorSeekCluster(fileEnumerator, entry))
        {
            if (entry < fileEnumerator->firstCluster + fileEnumerator->numClusters - 1)
		    { 
                value = (entry + 1);                                // Entry 2...(end-1): Next cluster of file
            }
            else
            {
                value = 0x0fffffff;                                 // Entry (end): End of chain (0xffff)
            }
        }
        else { value = 0x0ffffff7; }                                // Entry > (end-1): Bad sector (0xfff7)

        if (partition->fatType == VIRTUALDISK_FAT16)
        {
			// Write 16-bit entry
			p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8);
            p += 2;
			entry++;
            i += 2;
        }
        else if (partition->fatType == VIRTUALDISK_FAT32)
        {
			// Write 32-bit entry
			p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); p[2] = (unsigned char)(value >> 16); p[3] = (unsigned char)(value >> 24);
            p += 4;
			entry++;
            i += 4;
        }
        else if (partition->fatType == VIRTUALDISK_FAT12)
        {
			switch (part)
			{
				// 0: aa	(part 0)
				// 1: ba	(part 1, also part 10 = nibble 2 of byte 1)
				// 2: bb	(part 2)
				case  0:  *p =  (value     ) & 0xff; p++; i++; part =  1;          break;
				case  1:  *p =  (value >> 8) & 0x0f;           part = 10; entry++; break;
				case 10:  *p |= (value << 4) & 0xf0; p++; i++; part =  2;          break;
				case  2:  *p =  (value >> 4) & 0xff; p++; i++; part =  0; entry++; break;
				default:  *p++ = 0xff; i++; break;	// Shouldn't happen, but let's not cause an infinite loop if it does!
			}
        }
        else { break; }

	}

    return 1;
}


// (Private) Generate a sector of directory entries
unsigned short VirtualDiskPartitionGenerateDirectory(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer)
{
    virtualdisk_partition_t *partition = (virtualdisk_partition_t *)reference;
    int entriesPerSector = (partition->disk->sectorSize / 32);
    int i;
    virtualdisk_file_enumerator_t *fileEnumerator = &partition->fileEnumerator;

    // Update file enumerator to the current offset
    VirtualDiskFileEnumeratorSeekId(fileEnumerator, sector * entriesPerSector);   // Calculate first file index for this sector

    // Start with an empty sector
    memset(buffer, 0, partition->disk->sectorSize);

    // For each of the file indices in this sector...
    for (i = 0; i < entriesPerSector; i++)
    {
        if (fileEnumerator->hasFile)
        {
            const char *s;
            unsigned char *p;
            unsigned long cluster;
            int j;

            p = buffer + (i * 32);

            // Copy filename as expected by FAT
            memset(p, ' ', 11);
            s = fileEnumerator->fileInfo.filename;
            for (j = 0; j < 12; s++)
            {
                char c = *s;
                if (c == '\0') { break; }
                else if (c == '.') { j = 8; }
                else
                {
                    if (c >= 'a' && c <= 'z') { c = c - 'a' + 'A'; }

                    // Additional checking...
                    //if (c == 0xE5) { c = 0x05; }
                    //else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' || c == '@' || c == '~' || c == '`' || c == '!' || c == '(' || c == ')' || c == '{' || c =='}' || c == '^' || c == '#' || c == '&' || c >= 0x80) { ; }
                    //else { c = '_'; }

                    p[j] = (unsigned char)c;
                    j++;
                }
            }

            p[11] = fileEnumerator->fileInfo.attributes;                        // Attributes (+A=0x20,+R=0x01,volume=0x08)
            p[12] = 0x00;                                                       // Reserved (case information)
            p[13] = (fileEnumerator->fileInfo.created & 1) ? 100 : 0;           // Create time fine resolution (10ms unit, 0-199)
            SET_DATETIME_FAT_TIME(p + 14, fileEnumerator->fileInfo.created);    // Create time (00:00) [15-11=H, 10-5=M, 4-0=S/2]
            SET_DATETIME_FAT_DATE(p + 16, fileEnumerator->fileInfo.created);    // Create date (01/01/10) [15-9=Y, 8-5=M1, 4-0=D1]
            SET_DATETIME_FAT_DATE(p + 18, fileEnumerator->fileInfo.accessed);   // Last access date (01/01/10) [15-9=Y, 8-5=M1, 4-0=D1]
            SET_WORD(p + 20, 0x0000);                                           // EA-index
            SET_DATETIME_FAT_TIME(p + 22, fileEnumerator->fileInfo.modified);   // Last modified time (00:00) [15-11=H, 10-5=M, 4-0=S/2]
            SET_DATETIME_FAT_DATE(p + 24, fileEnumerator->fileInfo.modified);   // Last modified date (01/01/10) [15-9=Y, 8-5=M1, 4-0=D1]
            cluster = 0;                                                        // First FAT data cluster is at 2 (0 and 1 are reserved). Zero length files, such as volume labels, set to 0.
            if (fileEnumerator->fileInfo.size > 0)  //  && !(fileEnumerator->fileInfo.attributes & VIRTUALDISK_ATTRIB_VOLUME))
            {
                cluster = fileEnumerator->firstCluster;
            }
            SET_WORD(p + 26, (unsigned short)cluster);                           // Lower 16-bits of cluster
            SET_WORD(p + 20, (unsigned short)(cluster >> 16));                   // Upper 16-bits of cluster
            SET_DWORD(p + 28, fileEnumerator->fileInfo.size);
        }

        // Next file
        VirtualDiskFileEnumeratorNext(fileEnumerator);
            
    }
    return 1;
}


// (Private) Generate a NULL data sector
unsigned short VirtualDiskGenerateNull(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer)
{
    virtualdisk_t *disk = (virtualdisk_t *)reference;
    memset(buffer, 0x00, count * disk->sectorSize);
    return count;
}


// (Private) Determine which generator function to call for a partition
char VirtualDiskPartitionGetGenerator(virtualdisk_partition_t *partition, virtualdisk_generator_info_t *generatorInfo, unsigned long sector)
{
    const unsigned long addressFAT = partition->sectorsReserved;                                        // Start of FAT0
    const unsigned long addressRootDir = (addressFAT + (partition->sectorsFat0 * partition->numFat));   // Root directory
    const unsigned long addressFileContents = (addressRootDir + partition->sectorsRootDir);             // File contents

    generatorInfo->reference = partition;

	if (sector < addressFAT)                            // ---------- Boot sector ---------- 
	{
        generatorInfo->generator = VirtualDiskPartitionGenerateReserved;
        generatorInfo->firstSector = 0;
        generatorInfo->lastSector = addressFAT - 1;
        return 1;
    }
	else if (sector < addressRootDir)                   // ---------- FAT0 contents ---------- 
	{
        generatorInfo->generator = VirtualDiskPartitionGenerateFAT;
        generatorInfo->firstSector = addressFAT;
        generatorInfo->lastSector = addressRootDir - 1;
        return 1;
	}
    else if (sector < addressFileContents)              // ---------- Root directory ----------
	{
        generatorInfo->generator = VirtualDiskPartitionGenerateDirectory;
        generatorInfo->firstSector = addressRootDir;
        generatorInfo->lastSector = addressFileContents - 1;
        return 1;
	}
    else //if (sector < partition->partitionSizeSectors)  // ---------- File contents ----------
	{
        unsigned long dataCluster = (sector - addressFileContents) / partition->sectorsPerCluster;
        unsigned short clusterOffset = 2;   // Cluster 'address' needs the two reserved clusters adding
#ifdef VIRTUALDISK_HACK_FAT32
        if (partition->fatType == VIRTUALDISK_FAT32)
        {
            clusterOffset += (unsigned short)(partition->sectorsRootDir / partition->sectorsPerCluster);
        }
#endif
        if (VirtualDiskFileEnumeratorSeekCluster(&partition->fileEnumerator, dataCluster + clusterOffset))
        {
            generatorInfo->reference = &partition->fileEnumerator.fileInfo;
            generatorInfo->generator = partition->fileEnumerator.fileInfo.contents;
            generatorInfo->firstSector = addressFileContents + ((partition->fileEnumerator.firstCluster - clusterOffset) * partition->sectorsPerCluster);
            generatorInfo->lastSector = addressFileContents + ((partition->fileEnumerator.firstCluster - clusterOffset + partition->fileEnumerator.numClusters) * partition->sectorsPerCluster - 1);
            return 1;
        }
    }

    // Off the end of the partition
    return 0;
}


// (Private) Determine which generator function to call for a disk
char VirtualDiskGetGenerator(virtualdisk_t *disk, virtualdisk_generator_info_t *generatorInfo, unsigned long sector)
{
    int i;

    // Check if it's the MBR
    if (sector <= 0)                        // ---------- Master boot record ---------- 
	{
        generatorInfo->generator = VirtualDiskGenerateMBR;
        generatorInfo->reference = disk;
        generatorInfo->firstSector = sector;
        generatorInfo->lastSector = sector;
        return 1;
    }

    // Check partitions
    for (i = 0; i < disk->numPartitions; i++)
    {
        // Blank space before a partition
        if (sector < disk->partitions[i]->partitionStartSector)
        {
            generatorInfo->generator = VirtualDiskGenerateNull;
            generatorInfo->reference = disk;
            generatorInfo->firstSector = sector;        // Could be earlier
            generatorInfo->lastSector = disk->partitions[i]->partitionStartSector - 1;
            return 1;
        }

        if (sector >= disk->partitions[i]->partitionStartSector && sector < disk->partitions[i]->partitionStartSector + disk->partitions[i]->partitionSizeSectors)
        {
            if (VirtualDiskPartitionGetGenerator(disk->partitions[i], generatorInfo, sector - disk->partitions[i]->partitionStartSector))
            {
                // Adjust generator limits for partition's offset
                generatorInfo->firstSector += disk->partitions[i]->partitionStartSector;
                generatorInfo->lastSector += disk->partitions[i]->partitionStartSector;
                return 1;
            }
            break;
        }
    }

    // Blank space after partitions
    generatorInfo->generator = VirtualDiskGenerateNull;
    generatorInfo->reference = disk;
    generatorInfo->firstSector = sector;                // Could be earlier
    generatorInfo->lastSector = disk->sectorCount - 1;

    if (sector >= disk->sectorCount) { return 0; }      // Off the end of the disk

    return 1;
}


unsigned short VirtualDiskReadSectors(virtualdisk_t *disk, unsigned long sector, unsigned short count, void *buffer)
{
    unsigned short totalSectors = 0;

    if (!disk->initialized) { return 0; }

    // While we still have sectors to read
    while (count > 0)
    {
        unsigned short contiguous;

        // Check whether we can use the current generator
        if (disk->generatorInfo.generator == NULL || sector < disk->generatorInfo.firstSector || sector > disk->generatorInfo.lastSector)
        {
#ifdef VIRTUALDISK_DEBUG
            printf("! GET-GENERATOR\n");
#endif
            // If not, find the required generator
            if (!VirtualDiskGetGenerator(disk, &disk->generatorInfo, sector))
            {
                // None found
                disk->generatorInfo.generator = NULL;
            }
        }

        // If a generator was found
        if (disk->generatorInfo.generator != NULL)
        {
#ifdef VIRTUALDISK_DEBUG
            const char *label = "?";
            if (disk->generatorInfo.generator == VirtualDiskGenerateMBR) { label = "MBR"; }
            else if (disk->generatorInfo.generator == VirtualDiskPartitionGenerateReserved) { label = "Reserved"; }
            else if (disk->generatorInfo.generator == VirtualDiskPartitionGenerateFAT) { label = "FAT"; }
            else if (disk->generatorInfo.generator == VirtualDiskPartitionGenerateDirectory) { label = "Directory"; }
            else if (disk->generatorInfo.generator == VirtualDiskGenerateNull) { label = "Null"; }
            else { label = "Data?"; }
            printf("GENERATE: #%ld - @%ld = %s(%ld/%ld)\n", sector, disk->generatorInfo.firstSector, label, sector - disk->generatorInfo.firstSector, disk->generatorInfo.lastSector - disk->generatorInfo.firstSector);
#endif
            // Generate sectors
            contiguous = disk->generatorInfo.generator(disk->generatorInfo.reference, sector - disk->generatorInfo.firstSector, count, buffer);
        } 
        else 
        { 
            contiguous = 0; 
        }

        // If no sectors could be generated, that's an error
        if (contiguous <= 0)
        {
            // Skip this sector
            contiguous = 1;
            memset(buffer, 0xff, disk->sectorSize * contiguous);
        }

        // Adjust for next request, based on the sectors generated
        totalSectors += contiguous;
        count -= contiguous;
        sector += contiguous;
        buffer = (unsigned char *)buffer + (contiguous * disk->sectorSize);
    }

    return totalSectors;
}


unsigned short VirtualDiskSectorSize(virtualdisk_t *disk)
{
    if (!disk->initialized) { return 0; }
    return disk->sectorSize;
}


unsigned long VirtualDiskSectorCount(virtualdisk_t *disk)
{
    if (!disk->initialized) { return 0; }
    return disk->sectorCount;
}
