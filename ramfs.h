#ifndef __RAMFS_H__
#define __RAMFS_H__

#include <stdint.h>
#include <filesystem.h>

#define MAX_INODE_BLOCK_COUNT 16
#define BLOCK_SIZE 4096


int ramfs_read_superblock(void* opaque, struct superblock_t* sb);

fs_type_t ramfs_r = {
    .type_name_hash = 194671278;
    .rsbcb = ramfs_read_superblock;
    .require_dev = 0;
    .next = NULL;
};

typedef struct ramfs_inode_t{
    uint32_t hash;
    uint32_t attribute;
    char filename[64];
    uint32_t data_length;
    uint32_t block_count;
    uint32_t blocks[MAX_INODE_BLOCK_COUNT];
}ramfs_inode_t;

typedef struct ramfs_block_t {
    uint8_t data[BLOCK_SIZE];
}ramfs_block_t;

typedef struct ramfs_superblock_t{
    uint32_t inode_count;
    uint32_t block_count;
    ramfs_inode_t** inode_list;
    ramfs_block_t** block_pool;
}ramfs_superblock_t;

void register_ramfs(const char * mountpoint);
ramfs_inode_t* ramfs_get_file_by_hash(const ramfs_superblock_t * romfs, uint32_t h);

#endif

