#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vsfs.h"
#include <stdint.h>
#include <string.h>

// Superblock: 1 block, FAT table: 32 blocks, Root directory: 8 blocks, Data blocks: rest of the blocks

#define MAX_FILENAME_LENGTH 30 // 30 chars
#define MAX_DIRECTORY_ENTRIES 128
#define MAX_DIRECTORY_ENTRIES_PER_BLOCK 16
#define SUPERBLOCK_SIZE 1
#define FAT_SIZE 32
#define MAX_OPEN_FILES 16
#define MAX_DISK_SIZE 8388608 // 8MB
#define MIN_DISK_SIZE 262144 // 256kb 
#define ROOT_DIR_BLOCK 8

struct Superblock  { // 1 block
    uint32_t size;       // Total size of the virtual disk
    uint32_t block_size; // Size of each block
    uint32_t fat_start;  // Starting block number of the FAT
    uint32_t fat_blocks; // Number of blocks in the FAT
    uint32_t root_dir_start; // Starting block number of the root directory
    uint32_t root_dir_blocks; // Number of blocks in the root directory
    uint32_t data_start; // Starting block number of the data block
    uint32_t data_blocks; // Number of blocks in the data block
    uint8_t padding[2048 - 8 * sizeof(uint32_t)]; // Padding to fill up to 2048 bytes
};

struct FATEntry { // 4 bytes
    uint32_t next; // Next block pointer
};

struct DirectoryEntry { // 128 bytes
    char filename[MAX_FILENAME_LENGTH];
    uint32_t size;       // Size of the file in bytes
    uint32_t start_block; // Start data block number
    uint32_t current_position; // Current position in the file
    uint8_t is_used;      // Flag indicating whether the entry is in use (1) or not (0)
    uint8_t padding[128 - MAX_FILENAME_LENGTH - 4 * sizeof(uint32_t) - sizeof(uint8_t)]; // Padding to fill up to 128 bytes
};

// struct OpenFileEntry {
//     uint8_t is_used;           // Flag indicating whether the entry is in use (1) or not (0)
//     struct DirectoryEntry file_entry; // Corresponding directory entry
//     uint32_t current_position; // Current position in the file
// };

struct FileSystemMetadata {
    struct Superblock superblock;
    struct FATEntry fat_table[16384]; // Assuming maximum 16384 entries in FAT
    struct DirectoryEntry root_directory[128];
};

struct VirtualDisk {
    struct FileSystemMetadata metadata;
    uint8_t *data;
};

// globals  =======================================

int vs_fd; // file descriptor of the Linux file that acts as virtual disk. // this is not visible to an application.
// struct OpenFileEntry open_file_table[MAX_OPEN_FILES];
struct VirtualDisk virtual_disk;
// initialize it to all -1's. -1 indicates that the entry is free.
int OpenFileTable[16] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

int OpenFileModes[16] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};
// ========================================================

// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk. 
int read_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = read (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
        printf ("read error\n");
        return -1;
    }
    return 0; 
}

// write block k into the virtual disk. 
int write_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = write (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
        printf ("write error\n");
        return (-1);
    }
    return 0; 
}

void display_metadata() {
    printf("Superblock:\n");
    printf("size: %d\n", virtual_disk.metadata.superblock.size);
    printf("block_size: %d\n", virtual_disk.metadata.superblock.block_size);
    printf("fat_start: %d\n", virtual_disk.metadata.superblock.fat_start);
    printf("fat_blocks: %d\n", virtual_disk.metadata.superblock.fat_blocks);
    printf("root_dir_start: %d\n", virtual_disk.metadata.superblock.root_dir_start);
    printf("root_dir_blocks: %d\n", virtual_disk.metadata.superblock.root_dir_blocks);
    printf("data_start: %d\n", virtual_disk.metadata.superblock.data_start);
    printf("data_blocks: %d\n", virtual_disk.metadata.superblock.data_blocks);
}

int vsformat(char *vdiskname, unsigned int m) {
    vs_fd = open(vdiskname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (vs_fd == -1) {
        perror("Error opening virtual disk file");
        return -1;
    }

    uint32_t disk_size = 1 << m;
    if (ftruncate(vs_fd, disk_size) == -1) {
        perror("Error truncating virtual disk file");
        close(vs_fd);
        return -1;
    }

    struct FileSystemMetadata fs_metadata;

    // Initialize superblock
    fs_metadata.superblock.size = disk_size;
    fs_metadata.superblock.block_size = BLOCKSIZE;
    fs_metadata.superblock.fat_start = SUPERBLOCK_SIZE;
    fs_metadata.superblock.fat_blocks = FAT_SIZE;
    fs_metadata.superblock.root_dir_start = SUPERBLOCK_SIZE + FAT_SIZE;
    fs_metadata.superblock.root_dir_blocks = ROOT_DIR_BLOCK;
    fs_metadata.superblock.data_start = SUPERBLOCK_SIZE + FAT_SIZE + ROOT_DIR_BLOCK;
    fs_metadata.superblock.data_blocks = disk_size / BLOCKSIZE - SUPERBLOCK_SIZE - FAT_SIZE - ROOT_DIR_BLOCK;

    // write the superblock to the disk
    if (write_block(&fs_metadata.superblock, 0) != 0) {
        perror("Error writing superblock to virtual disk");
        close(vs_fd);
        return -1;
    }

    // Initialize FAT table
    for (int i = 0; i < 16384; i++) {
        fs_metadata.fat_table[i].next = 0xFFFFFFFF; // Set all entries to an invalid value (0xFFFFFFFF)
    }

    // write the FAT table to the disk
    for (int i = 0; i < FAT_SIZE; i++) {
        if (write_block(&fs_metadata.fat_table[i], SUPERBLOCK_SIZE + i) != 0) {
            perror("Error writing FAT table to virtual disk");
            close(vs_fd);
            return -1;
        }
    }

    // Initialize root directory
    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        memset(fs_metadata.root_directory[i].filename, 0, MAX_FILENAME_LENGTH);
        fs_metadata.root_directory[i].size = 0;
        fs_metadata.root_directory[i].start_block = 0;
        fs_metadata.root_directory[i].current_position = 0;
        fs_metadata.root_directory[i].is_used = 0;
    }

    // write the root directory to the disk
    for (int i = 0; i < ROOT_DIR_BLOCK; i++) {
        if (write_block(&fs_metadata.root_directory[i], SUPERBLOCK_SIZE + FAT_SIZE + i) != 0) {
            perror("Error writing root directory to virtual disk");
            close(vs_fd);
            return -1;
        }
    }

    // if (write_block(&fs_metadata, 0) != 0) {
    //     perror("Error writing superblock to virtual disk");
    //     close(vs_fd);
    //     return -1;
    // }

    // Close the virtual disk file
    close(vs_fd);

    return 0;
}

int vsmount(char *vdiskname) {
    vs_fd = open(vdiskname, O_RDWR);
    if (vs_fd == -1) {
        perror("Error opening virtual disk file for mount");
        return -1;
    }

    // read the superblock from the disk
    if (read_block(&virtual_disk.metadata.superblock, 0) == -1) {
        printf("Error reading superblock from disk\n");
        return -1;
    }

    // read the FAT table from the disk
    for (int i = 0; i < FAT_SIZE; i++) {
        if (read_block(&virtual_disk.metadata.fat_table[i], SUPERBLOCK_SIZE + i) != 0) {
            printf("Error reading block %d\n", i);
            return -1;
        }
    }

    // read the root directory from the disk
    for (int i = 0; i < ROOT_DIR_BLOCK; i++) {
        if (read_block(&virtual_disk.metadata.root_directory[i], SUPERBLOCK_SIZE + FAT_SIZE + i) != 0) {
            printf("Error reading block %d\n", i);
            return -1;
        }
    }

    return 0;
}

int vsumount (){
    if (write_block(&virtual_disk.metadata, 0) != 0) {
        perror("Error writing superblock to virtual disk");
        close(vs_fd);
        return -1;
    }

    if (write_block(&virtual_disk.metadata.fat_table, virtual_disk.metadata.superblock.fat_start) != 0) {
        perror("Error writing FAT table to virtual disk");
        close(vs_fd);
        return -1;
    }

    if (write_block(&virtual_disk.metadata.root_directory, virtual_disk.metadata.superblock.root_dir_start) != 0) {
        perror("Error writing root directory to virtual disk");
        close(vs_fd);
        return -1;
    }

    printf("Unmounted vs_fd %d\n", vs_fd);

    close(vs_fd);

    return 0;
}

int find_free_directory_entry(struct DirectoryEntry *root_directory) {
    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if (!root_directory[i].is_used) {
            return i;
        }
    }
    return -1;
}

// Helper function to update the root directory on the virtual disk
int update_root_directory() {
    if (write_block(&virtual_disk.metadata.root_directory, virtual_disk.metadata.superblock.root_dir_start) != 0) {
        perror("Error updating root directory on virtual disk");
        return -1;
    }
    return 0;
}

int vscreate(char *filename) {
    struct DirectoryEntry *root_directory = virtual_disk.metadata.root_directory;

    int free_entry_index = find_free_directory_entry(root_directory);
    if (free_entry_index == -1) {
        perror("Error: Root directory is full");
        return -1;
    }

    strncpy(root_directory[free_entry_index].filename, filename, MAX_FILENAME_LENGTH);
    root_directory[free_entry_index].size = 0;
    root_directory[free_entry_index].start_block = 0;
    root_directory[free_entry_index].current_position = 0;
    root_directory[free_entry_index].is_used = 1;

    if (update_root_directory() != 0) {
        return -1;
    }

    return 0;
}

int vsopen(char *filename, int mode) {
    struct DirectoryEntry *root_directory = virtual_disk.metadata.root_directory;

    int file_index = -1;
    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if (root_directory[i].is_used && strcmp(root_directory[i].filename, filename) == 0) {
            file_index = i;
            break;
        }
    }

    if (file_index == -1) {
        perror("Error: File not found");
        return -1;
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (OpenFileTable[i] == file_index) {
            printf("Error: File is already open\n");
            return -1;
        }
    }

    int open_file_index = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (OpenFileTable[i] == -1) {
            open_file_index = i;
            break;
        }
    }

    if (open_file_index == -1) {
        perror("Error: Open file table is full");
        return -1;
    }

    OpenFileTable[open_file_index] = file_index;
    OpenFileModes[open_file_index] = mode;

    printf("File opened %s\n", filename);
    return open_file_index;
}

int vssize(int fd) {
    struct DirectoryEntry *root_directory = virtual_disk.metadata.root_directory;

    if (fd < 0 || fd >= MAX_OPEN_FILES || OpenFileTable[fd] == -1) {
        perror("Error: Invalid file descriptor");
        return -1;
    }

    int file_index = OpenFileTable[fd];

    if (file_index == -1) {
        perror("Error: File is not open");
        return -1;
    }

    uint32_t file_size = root_directory[file_index].size;

    printf("%d\n",file_size);
    return file_size;
}

int vsclose(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || OpenFileTable[fd] == -1) {
        perror("Error: Invalid file descriptor");
        return -1;
    }

    int file_index = OpenFileTable[fd];

    if (file_index == -1) {
        perror("Error: File is not open");
        return -1;
    }

    OpenFileTable[fd] = -1;

    printf("File closed (descriptor %d)\n", fd);

    return 0;
}


int vsread(int fd, void *buf, int n) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || OpenFileTable[fd] == -1) {
        perror("Error: Invalid file descriptor");
        return -1;
    }

    int file_index = OpenFileTable[fd];

    if (file_index == -1) {
        perror("Error: File is not open");
        return -1;
    }

    struct DirectoryEntry *root_directory = virtual_disk.metadata.root_directory;

    uint32_t current_position = root_directory[file_index].current_position;

    uint32_t available_bytes = root_directory[file_index].size - current_position;

    int bytes_to_read = (n < available_bytes) ? n : available_bytes;

    int start_block = root_directory[file_index].start_block;
    int remaining_bytes = bytes_to_read;
    int offset = 0;

    while (remaining_bytes > 0) {
        // Calculate the block number to read
        int block_number = start_block + (current_position / BLOCKSIZE);
        // Calculate the offset within the block
        int block_offset = current_position % BLOCKSIZE;
        // Calculate the number of bytes to read in this iteration
        int read_size = (remaining_bytes < BLOCKSIZE - block_offset) ? remaining_bytes : BLOCKSIZE - block_offset;

        // Read the block from the virtual disk
        if (read_block(buf + offset, block_number) != 0) {
            perror("Error reading data from the file");
            return -1;
        }

        // Update variables for the next iteration
        current_position += read_size;
        offset += read_size;
        remaining_bytes -= read_size;
    }

    root_directory[file_index].current_position = current_position;

    return bytes_to_read;
}

int vsappend(int fd, void *buf, int n) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || OpenFileTable[fd] == -1 || OpenFileModes[fd] != MODE_APPEND) {
        perror("Error: Invalid file descriptor or file is not open in append mode");
        return -1;
    }

    int file_index = OpenFileTable[fd];

    if (file_index == -1) {
        perror("Error: File is not open");
        return -1;
    }

    struct DirectoryEntry *root_directory = virtual_disk.metadata.root_directory;

    uint32_t current_position = root_directory[file_index].current_position;

    int start_block = root_directory[file_index].start_block;
    int block_number = start_block + (current_position / BLOCKSIZE);
    int block_offset = current_position % BLOCKSIZE;

    int remaining_bytes = n;
    int offset = 0;

    while (remaining_bytes > 0) {
        // Calculate the number of bytes to append in this iteration
        int append_size = (remaining_bytes < BLOCKSIZE - block_offset) ? remaining_bytes : BLOCKSIZE - block_offset;

        // Write data to the file at the calculated block and offset
        if (write_block(buf + offset, block_number) != 0) {
            perror("Error writing data to the file");
            return -1;
        }

        // Update variables for the next iteration
        current_position += append_size;
        offset += append_size;
        remaining_bytes -= append_size;

        if (remaining_bytes > 0) {
            perror("Error: Not enough space for the data");
            return -1;
        }
    }

    root_directory[file_index].current_position = current_position;

    root_directory[file_index].size = current_position;

    return n;
}

int vsdelete(char *filename) {
    if (filename == NULL || strlen(filename) > MAX_FILENAME_LENGTH) {
        printf("Error: Invalid filename\n");
        return -1;
    }

    int entryIndex = -1;
    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if (virtual_disk.metadata.root_directory[i].is_used &&
            strcmp(virtual_disk.metadata.root_directory[i].filename, filename) == 0) {
            entryIndex = i;
            break;
        }
    }

    if (entryIndex == -1) {
        printf("Error: File not found\n");
        return -1;
    }

    virtual_disk.metadata.root_directory[entryIndex].is_used = 0;

    virtual_disk.metadata.root_directory[entryIndex].size = 0;
    virtual_disk.metadata.root_directory[entryIndex].start_block = 0;
    memset(virtual_disk.metadata.root_directory[entryIndex].filename, 0, MAX_FILENAME_LENGTH);

    // Update the root directory on the virtual disk
    if (write_block(&virtual_disk.metadata.root_directory, SUPERBLOCK_SIZE + FAT_SIZE) == -1) {
        printf("Error writing root directory to disk\n");
        return -1;
    }

    printf("%s deleted\n", filename);
    return 0;
}