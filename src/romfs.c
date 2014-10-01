#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "fio.h"
#include "filesystem.h"
#include "romfs.h"
#include "osdebug.h"
#include "hash-djb2.h"

#include "clib.h"

struct romfs_file_t{
    uint32_t hash;
    uint32_t filename_length;
    uint8_t attribute;
    uint32_t length;
    uint32_t data_offset;
}__attribute__((packed));

struct romfs_fds_t {
    const struct romfs_file_t * file_des;
    const uint8_t* data;
    const uint8_t* opaque;
    uint32_t cursor;
};

static struct romfs_fds_t romfs_fds[MAX_FDS];
static struct romfs_fds_t romfs_dds[MAX_FDS];

/*
static uint32_t get_unaligned(const uint8_t * d) {
    return ((uint32_t) d[0]) | ((uint32_t) (d[1] << 8)) | ((uint32_t) (d[2] << 16)) | ((uint32_t) (d[3] << 24));
}
*/

const uint8_t* get_data_address(const struct romfs_file_t* file, const uint8_t* romfs){
    return romfs + ((sizeof(struct romfs_file_t) * (*((uint32_t*)romfs))) + 4 + file->data_offset);
}

static ssize_t romfs_read(void * opaque, void * buf, size_t count) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    uint32_t size = f->file_des->length;
    
    if ((f->cursor + count) > size)
        count = size - f->cursor;

    memcpy(buf, f->data + f->file_des->filename_length + f->cursor, count);
    f->cursor += count;

    return count;
}

static ssize_t romfs_readdir(void * opaque, struct dir_entity_t* ent) {
    struct romfs_fds_t * dir = (struct romfs_fds_t *) opaque;
    uint32_t* file_hashes = (uint32_t*)(dir->data + dir->file_des->filename_length);
    uint32_t file_count = *(file_hashes++);
    const struct romfs_file_t * file;

    if(dir->cursor >= file_count)
        return -2;

    file = romfs_get_file_by_hash(dir->opaque, file_hashes[dir->cursor], NULL);
    strncpy(ent->d_name, (char*)get_data_address(file, dir->opaque), file->filename_length);
    ent->d_name[file->filename_length] = '\0';

    ent->d_attr = file->attribute;

    dir->cursor++;
    return 0;
}

static off_t romfs_seek(void * opaque, off_t offset, int whence) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
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

const struct romfs_file_t * romfs_get_file_by_hash(const uint8_t * romfs, uint32_t h, uint32_t * len) {
    uint32_t file_count = (*(uint32_t*)romfs);
    const struct romfs_file_t* meta = (struct romfs_file_t*)(romfs + 4);
    for (uint32_t i = 0; i < file_count; i++) {
        if (meta[i].hash == h) {
            if (len) {
                *len = meta[i].length;
            }
            return meta + i;
        }
    }

    return NULL;
}

/*
static int romfs_list(void * opaque, char*** path) {
    uint8_t* romfs = opaque;
    uint32_t file_count = (*(uint32_t*)romfs);
    const struct romfs_file_t* meta = (struct romfs_file_t*)(romfs + 4);

    (*path) = (char**)pvPortMalloc(sizeof(char*) * file_count);
    uint32_t i;

    for (i = 0; i < file_count; i++) {
	(*path)[i] = (char*)pvPortMalloc(sizeof(char) * meta[i].filename_length + 1);
	strncpy((*path)[i], (char*)get_data_address(meta + i, romfs), meta[i].filename_length);
	strcat((*path)[i], '\0');
    }

    return i;
}
*/

static int romfs_open(void * opaque, const char * path, int flags, int mode) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
    const uint8_t * romfs = (const uint8_t *) opaque;
    const struct romfs_file_t * file;
    int r = -1;

    file = romfs_get_file_by_hash(romfs, h, NULL);

    if (file) {
        r = fio_open(romfs_read, NULL, romfs_seek, NULL, NULL);
        if (r > 0) {
            romfs_fds[r].file_des = file;
            romfs_fds[r].data = get_data_address(file, romfs);
            romfs_fds[r].cursor = 0;
            fio_set_opaque(r, romfs_fds + r);
        }
    }
    return r;
}

static int romfs_opendir(void * opaque, char * path) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
    const uint8_t * romfs = (const uint8_t *) opaque;
    const struct romfs_file_t * file;
    int r = -1;

    file = romfs_get_file_by_hash(romfs, h, NULL);

    if (file) {
        r = fio_opendir(romfs_readdir, NULL, NULL);
        if (r >= 0) {
            romfs_dds[r].file_des = file;
            romfs_dds[r].data = get_data_address(file, romfs);
            romfs_dds[r].opaque = romfs;
            romfs_dds[r].cursor = 0;
            fio_set_dir_opaque(r, romfs_dds + r);
        }
    }
    return r;
}

void register_romfs(const char * mountpoint, const uint8_t * romfs) {
//    DBGOUT("Registering romfs `%s' @ %p\r\n", mountpoint, romfs);
    register_fs(mountpoint, romfs_open, romfs_opendir, (void *) romfs);
}
