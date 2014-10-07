#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "fio.h"
#include "filesystem.h"
#include "ramfs.h"
#include "osdebug.h"
#include "hash-djb2.h"

#include "clib.h"

#define MAX_INODE_BLOCK_COUNT 16
#define BLOCK_SIZE 4096

typedef struct ramfs_inode_t{
    uint32_t hash;
    uint32_t attribute;
    uint32_t filename_length;
    char filename[64];
    uint32_t data_length;
    uint32_t blocks[MAX_INODE_BLOCK_COUNT];
};

typedef struct ramfs_block_t {
    uint8_t data[BLOCK_SIZE];
};

typedef struct ramfs_superblock_t{
    uint32_t inode_count;
    uint32_t block_count;
    ramfs_inode_t* inode_list;
    ramfs_block_t* block_pool;
};

struct ramfs_fds_t {
    const struct ramfs_file_t * file_des;
    const uint8_t* data;
    const uint8_t* opaque;
    uint32_t cursor;
};

static struct ramfs_fds_t ramfs_fds[MAX_FDS];
static struct ramfs_fds_t ramfs_dds[MAX_FDS];

const uint8_t* get_data_address(const struct ramfs_file_t* file, const uint8_t* ramfs){
    return ramfs + ((sizeof(struct ramfs_file_t) * (*((uint32_t*)ramfs))) + 4 + file->data_offset);
}

static ramfs_superblock_t* init_superblock(void){
    ramfs_superblock_t* ret = (ramfs_superblock_t*)calloc(sizeof(ramfs_superblock_t), 1);
    ret->innode_count = 0; 
    ret->block_count = 0;
    ret->inode_list = NULL;
    return ret;
}

static void deinit_superblock(ramfs_superblock_t* sb){
    if(!sb->inode_list)
        free(sb->inode_list);
    free(sb);
}

static ramfs_inode_t* add_inode(uint32_t h, const char* filename, ramfs_superblock_t* sb){
    ramfs_inode_t* ret = (ramfs_inode_t*)calloc(sizeof(ramfs_inode_t), 1);
    ret->hash = h;
    strcpy(ret->filename, filename);
    ret->filename_length = strlen(filename);
    ret->attribute = 0;
    ret->data_length = 0; 
    ret->blocks = NULL;

    ramfs_inode_t* src = sb->inode_list;
    sb->inode_list = (ramfs_inode_t*)calloc(sizeof(ramfs_inode_t), 1);
    memcpy(sb->inode_list, src, sizeof(ramfs_inode_t) * sb->inode_count);
    free(src);

    sb->inode_list[sb->inode_count++] = ret;
    return ret;
}

static ssize_t ramfs_read(void * opaque, void * buf, size_t count) {
    struct ramfs_fds_t * f = (struct ramfs_fds_t *) opaque;
    uint32_t size = f->file_des->length;
    
    if ((f->cursor + count) > size)
        count = size - f->cursor;

    memcpy(buf, f->data + f->file_des->filename_length + f->cursor, count);
    f->cursor += count;

    return count;
}

static ssize_t ramfs_readdir(void * opaque, struct dir_entity_t* ent) {
    struct ramfs_fds_t * dir = (struct ramfs_fds_t *) opaque;
    uint32_t* file_hashes = (uint32_t*)(dir->data + dir->file_des->filename_length);
    uint32_t file_count = *(file_hashes++);
    const struct ramfs_file_t * file;

    if(dir->cursor >= file_count)
        return -2;

    file = ramfs_get_file_by_hash(dir->opaque, file_hashes[dir->cursor], NULL);
    strncpy(ent->d_name, (char*)get_data_address(file, dir->opaque), file->filename_length);
    ent->d_name[file->filename_length] = '\0';

    ent->d_attr = file->attribute;

    dir->cursor++;
    return 0;
}

static off_t ramfs_seek(void * opaque, off_t offset, int whence) {
    struct ramfs_fds_t * f = (struct ramfs_fds_t *) opaque;
    uint32_t size = f->file_des->length;
    uint32_t origin;
    
    switch (whence) {
    case SEEK_SET:
        origin = 0;
        break;
    case SEEK_CUR:
        origin = f->cursor;
        break;
    case SEEK_END:
        origin = size;
        break;
    default:
        return -1;
    }

    offset = origin + offset;

    if (offset < 0)
        return -1;
    if (offset > size)
        offset = size;

    f->cursor = offset;

    return offset;
}

static off_t ramfs_seekdir(void * opaque, off_t offset) {
    struct ramfs_fds_t * dir = (struct ramfs_fds_t *) opaque;
    uint32_t file_count = *((uint32_t*)(dir->data + dir->file_des->filename_length));

    if(offset >= file_count || offset < 0)
        return -2;

    dir->cursor = offset;

    return offset;
}

const struct ramfs_inode_t * ramfs_get_file_by_hash(const ramfs_superblock_t* ramfs, uint32_t h) {
    for (uint32_t i = 0; i < ramfs->innode_count; i++) {
        if (ramfs->inode_list[i].hash == h) {
            return ramfs->inode_list[i];
        }
    }
    return NULL;
}

static int ramfs_open(void * opaque, const char * path, int flags, int mode) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
    const ramfs_superblock_t* sb = (const ramfs_superblock_t*) opaque;
    const struct ramfs_inode_t * file;
    int r = -1;

    file = ramfs_get_file_by_hash(h, path, sb);
    
    if(!file){
        file = add_inode(h, path, ramfs); 
    }

    if (file) {
        r = fio_open(ramfs_read, NULL, ramfs_seek, NULL, NULL);
        if (r > 0) {
            ramfs_fds[r].file_des = file;
            ramfs_fds[r].data = get_data_address(file, ramfs);
            ramfs_fds[r].cursor = 0;
            fio_set_opaque(r, ramfs_fds + r);
        }
    return r;
}

static int ramfs_opendir(void * opaque, char * path) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
    const uint8_t * ramfs = (const uint8_t *) opaque;
    const struct ramfs_file_t * file;
    int r = -1;

    file = ramfs_get_file_by_hash(ramfs, h, NULL);

    if (file) {
        r = fio_opendir(ramfs_readdir, ramfs_seekdir, NULL, NULL);
        if (r >= 0) {
            ramfs_dds[r].file_des = file;
            ramfs_dds[r].data = get_data_address(file, ramfs);
            ramfs_dds[r].opaque = ramfs;
            ramfs_dds[r].cursor = 0;
            fio_set_dir_opaque(r, ramfs_dds + r);
        }
    }
    return r;
}

void register_ramfs(const char * mountpoint, const uint8_t * ramfs) {
//    DBGOUT("Registering ramfs `%s' @ %p\r\n", mountpoint, ramfs);
    ramfs_superblock_t* sb = init_superblock();
    if(!sb)
        register_fs(mountpoint, ramfs_open, ramfs_opendir, (void *) sb);
}
