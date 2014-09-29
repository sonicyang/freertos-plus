#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "fio.h"
#include "filesystem.h"
#include "romfs.h"
#include "osdebug.h"
#include "hash-djb2.h"

struct romfs_file_t{
    uint32_t hash;
    uint32_t length;
    uint32_t filename_length;
    uint8_t filename;
    uint8_t data;
}__attribute__((packed));

struct romfs_fds_t {
    const struct romfs_file_t * file;
    uint32_t cursor;
};

static struct romfs_fds_t romfs_fds[MAX_FDS];

/*
static uint32_t get_unaligned(const uint8_t * d) {
    return ((uint32_t) d[0]) | ((uint32_t) (d[1] << 8)) | ((uint32_t) (d[2] << 16)) | ((uint32_t) (d[3] << 24));
}
*/

const uint32_t get_data_offset(const struct romfs_file_t* file){
    return sizeof(struct romfs_file_t) - 2 + file->filename_length;
}

static ssize_t romfs_read(void * opaque, void * buf, size_t count) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    uint32_t size = f->file->length;
    
    if ((f->cursor + count) > size)
        count = size - f->cursor;

    memcpy(buf, (uint8_t*)f->file + get_data_offset(f->file) + f->cursor, count);
    f->cursor += count;

    return count;
}

static off_t romfs_seek(void * opaque, off_t offset, int whence) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    uint32_t size = f->file->length;
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
    const uint8_t* meta;

    for (meta = romfs; ((struct romfs_file_t*)meta)->hash && ((struct romfs_file_t*)meta)->length; meta += ((struct romfs_file_t*)meta)->length +  ((struct romfs_file_t*)meta)->filename_length + (sizeof(struct romfs_file_t) - 2)) {
        if (((struct romfs_file_t*)meta)->hash == h) {
            if (len) {
                *len = ((struct romfs_file_t*)meta)->length;
            }
            return (struct romfs_file_t*)meta;
        }
    }

    return NULL;
}

static int romfs_list(void * opaque, char*** path) {
    uint32_t count = 0;    

    const uint8_t* meta;

    for (meta = opaque; ((struct romfs_file_t*)meta)->hash && ((struct romfs_file_t*)meta)->length; meta += ((struct romfs_file_t*)meta)->length +  ((struct romfs_file_t*)meta)->filename_length + (sizeof(struct romfs_file_t) - 2)) {
	count++;
    }
 
    (*path) = (char**)pvPortMalloc(sizeof(char*) * count);
    
    uint32_t i = 0;    

    for (meta = opaque; ((struct romfs_file_t*)meta)->hash && ((struct romfs_file_t*)meta)->length; meta += ((struct romfs_file_t*)meta)->length +  ((struct romfs_file_t*)meta)->filename_length + (sizeof(struct romfs_file_t) - 2)) {
	(*path)[i] = (char*)pvPortMalloc(sizeof(char) * ((struct romfs_file_t*)meta)->filename_length + 1);
	strncpy((*path)[i], (const char*)&((const struct romfs_file_t*)meta)->filename, ((struct romfs_file_t*)meta)->filename_length);
	strcat((*path)[i], '\0');
	i++;	
    }

    return i;
}

static int romfs_open(void * opaque, const char * path, int flags, int mode) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
    const uint8_t * romfs = (const uint8_t *) opaque;
    const struct romfs_file_t * file;
    int r = -1;

    file = romfs_get_file_by_hash(romfs, h, NULL);

    if (file) {
        r = fio_open(romfs_read, NULL, romfs_seek, NULL, NULL);
        if (r > 0) {
            romfs_fds[r].file = file;
            romfs_fds[r].cursor = 0;
            fio_set_opaque(r, romfs_fds + r);
        }
    }
    return r;
}

void register_romfs(const char * mountpoint, const uint8_t * romfs) {
//    DBGOUT("Registering romfs `%s' @ %p\r\n", mountpoint, romfs);
    register_fs(mountpoint, romfs_open, romfs_list, (void *) romfs);
}
