// Virtual Disk/File System
// Dan Jackson, 2013


#ifndef VIRTUALDISK_H
#define VIRTUALDISK_H

// Plain C linkage
#ifdef __cplusplus
extern "C" {
#endif

// Fixed values
#define VIRTUALDISK_MAX_PARTITIONS 4                    // Maximum number of primary partitions on the disk (must be 1-4)
#define VIRTUALDISK_DEFAULT_NUM_FAT 1                   // Number of FAT tables (1 is acceptable on removable media, but traditionally 2)

// Defaults for initialization
#define VIRTUALDISK_DEFAULT_SECTORS_PER_CLUSTER 0x40    // 0x40 -- 64 * sector_size = 32Kb clusters
#define VIRTUALDISK_DEFAULT_COUNT_DATA_CLUSTERS 65500   // e.g. 64768.  Count of the number of data clusters (max cluster entries will be count+2) FAT16 between 4085 and 65524, FAT12 below this and FAT32 above. Avoid values within 16 of the cut-off points as *many* implementations incorrectly identify the file-system around these points
#define VIRTUALDISK_DEFAULT_ROOT_DIR_ENTRIES    512     // Commonly 512. Should be a multiple of (sector_size/32) (e.g. a multiple of 16)
#define VIRTUALDISK_DEFAULT_SECTOR_SIZE         512     // Size of each sector -- should be a power of two and must be at least 512 bytes.  512 bytes is the most widely supported.

// Declaration
struct virtualdisk_fileinfo_struct_t;
struct virtualdisk_partition_struct_t;

// Date/time type -- not exactly as FAT's, but shifted up by one to include exact seconds, and years are stored from 2000 rather than 1980.
#define VIRTUALDISK_DATETIME(_year, _month, _day, _hours, _minutes, _seconds) ( (((unsigned long)(_year % 100) & 0x3f) << 26) | (((unsigned long)(_month) & 0x0f) << 22) | (((unsigned long)(_day) & 0x1f) << 17) | (((unsigned long)(_hours) & 0x1f) << 12) | (((unsigned long)(_minutes) & 0x3f) <<  6) | ((unsigned long)(_seconds) & 0x3f) )
#define VIRTUALDISK_DATETIME_MIN VIRTUALDISK_DATETIME(2000,1,1,0,0,0)
//#define VIRTUALDISK_DATETIME_MAX VIRTUALDISK_DATETIME(2063,12,31,23,59,59)

// File attributes
#define VIRTUALDISK_ATTRIB_READONLY     0x01
#define VIRTUALDISK_ATTRIB_HIDDEN       0x02
#define VIRTUALDISK_ATTRIB_SYSTEM       0x04
#define VIRTUALDISK_ATTRIB_VOLUME       0x08
#define VIRTUALDISK_ATTRIB_DIRECTORY    0x10
#define VIRTUALDISK_ATTRIB_ARCHIVE      0x20

// (Public) sector generator function
typedef unsigned short (*virtualdisk_generator_t)(void *reference, unsigned long sector, unsigned short count, unsigned char *buffer);

// (Public) File information structure
typedef struct virtualdisk_fileinfo_t_struct
{
    // Set by caller
    int id;                                     // Unique numeric identifier

    // Set by callee
    const char *filename;                       // Pointer to filename
    unsigned long size;                         // File size (bytes)
    unsigned char attributes;                   // File attributes
    unsigned long modified;                     // Modified date/time
    unsigned long created;                      // Created date/time
    unsigned long accessed;                     // Accessed date/time
    virtualdisk_generator_t contents;           // Function to generate file contents
    void *reference;                            // User-supplied reference for file generator
} virtualdisk_fileinfo_t;

// (Public) Type of the callback function to get file information
typedef char (*VirtualDiskFileInfoCallback)(virtualdisk_fileinfo_t *);


// (Private) File enumerator
typedef struct
{
    struct virtualdisk_partition_struct_t *partition; // Reference to partition containing file
    VirtualDiskFileInfoCallback fileInfoCallback;   // Function to generate file information
    char hasFile;                                   // Has file information (zero if no more files)
    virtualdisk_fileinfo_t fileInfo;                // File information for the current file
    unsigned long firstCluster;                     // First cluster index
    unsigned long numClusters;                      // Number of clusters
} virtualdisk_file_enumerator_t;


// (Private) Sector generator information
typedef struct
{
    // Reference for the generator
    void *reference;

    // Sector range the generator is valid for
    unsigned long firstSector;
    unsigned long lastSector;

    // Generator function
    virtualdisk_generator_t generator;

} virtualdisk_generator_info_t;


// FAT type
typedef enum
{
    VIRTUALDISK_FAT12, VIRTUALDISK_FAT16, VIRTUALDISK_FAT32
} VIRTUALDISK_FAT_TYPE;


// FAT Partition
typedef struct virtualdisk_partition_struct_t
{
    // Added values
    struct virtualdisk_struct_t *disk;              // Disk on which the partition lies
    unsigned long partitionStartSector;             // Location on the disk
    unsigned long binaryId;                         // Disk serial number

    // Calculated values
    unsigned long partitionSizeSectors;             // Virtual partition length in sectors

    // User-supplied FAT-specific values
    unsigned char sectorsPerCluster;                // e.g. 0x40 (=64) -->  64 * sector_size = 32Kb clusters
    unsigned long countDataClusters;                // e.g. 64768.  Count of the number of data clusters (max cluster entries will be count+2) FAT16 between 4085 and 65524
    unsigned short rootDirEntries;                  // Maximum number of root directory entries - should be a multiple of (sector_size/32=) 16
    VirtualDiskFileInfoCallback fileInfoCallback;   // Function to return information about files in the root directory of the partition

    // Calculated FAT-specific values
    VIRTUALDISK_FAT_TYPE fatType;                   // FAT sub-type (FAT12, FAT16, FAT32) determined by the number of clusters
    unsigned char numFat;                           // Number of FAT tables (1 or 2)
    unsigned short sectorsReserved;                 // Number of reserved sectors on the partition before the FAT, including the boot sector (at least 1)
    unsigned long sectorsRootDir;                   // Number of sectors in the root directory region
    unsigned long sectorsFat0;                      // Number of sectors in the FAT0 region
    unsigned long sectorsData;                      // Number of sectors in the data region
    unsigned long regionData;                       // Offset on the partition of the data region (also, the total number of sectors 'overhead' in the partition - those not in the data region)

    // Track file enumeration
    virtualdisk_file_enumerator_t fileEnumerator;   // File enumeration (mainly tracks cluster offset)

} virtualdisk_partition_t;


// Virtual disk
typedef struct virtualdisk_struct_t
{
    char initialized;

    unsigned short sectorSize;                      // Size of each sector in bytes (e.g. 512)
    unsigned long sectorCount;                      // Virtual disk length in sectors

    // Partitions
    virtualdisk_partition_t *partitions[VIRTUALDISK_MAX_PARTITIONS];
    int numPartitions;

    // Sector generator
    virtualdisk_generator_info_t generatorInfo;

} virtualdisk_t;



// ---------- Public API ----------

// (Public) Initialize a new virtual disk
char VirtualDiskInit(virtualdisk_t *disk, unsigned short sectorSize);

// (Public) Add a FAT partition to a disk
char VirtualDiskAddPartition(virtualdisk_t *disk, virtualdisk_partition_t *partition, VirtualDiskFileInfoCallback fileInfoCallback, unsigned char sectorsPerCluster, unsigned long countDataClusters, unsigned short rootDirEntries);

// (Public) Get the sector size of a disk (in bytes) - e.g. 512
unsigned short VirtualDiskSectorSize(virtualdisk_t *disk);

// (Public) Get the size of the disk (in sectors)
unsigned long VirtualDiskSectorCount(virtualdisk_t *disk);

// (Public) Read the specified virtual sectors into a memory buffer
unsigned short VirtualDiskReadSectors(virtualdisk_t *disk, unsigned long sector, unsigned short count, void *buffer);


#ifdef __cplusplus
}
#endif

#endif
