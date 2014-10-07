#ifndef __FIO_H__
#define __FIO_H__

#include <stdio.h>
#include <stdint.h>

enum open_types_t {
    O_RDONLY = 0,
    O_WRONLY = 1,                                                               
    O_RDWR = 2,
    O_CREAT = 4,
    O_TRUNC = 8,
    O_APPEND = 16,
};

#define MAX_FDS 32
#define MAX_DDS 4

struct dir_entity_t {
    uint8_t d_attr;
    char d_name[256];    
};

typedef ssize_t (*fdread_t)(void * opaque, void * buf, size_t count);
typedef ssize_t (*fdwrite_t)(void * opaque, const void * buf, size_t count);
typedef off_t (*fdseek_t)(void * opaque, off_t offset, int whence);
typedef int (*fdclose_t)(void * opaque);

typedef ssize_t (*ddread_t)(void * opaque, struct dir_entity_t* ent);
typedef off_t (*ddseek_t)(void * opaque, off_t offset);
typedef int (*ddclose_t)(void * opaque);

struct fddef_t {
    fdread_t fdread;
    fdwrite_t fdwrite;
    fdseek_t fdseek;
    fdclose_t fdclose;
    void * opaque;
};

struct dddef_t {
    ddread_t ddread;
    ddseek_t ddseek;
    ddclose_t ddclose;
    void * opaque;
};

/* Need to be called before using any other fio functions */
__attribute__((constructor)) void fio_init();

int fio_dir_is_open(int dd);
int fio_opendir(ddread_t, ddseek_t, ddclose_t, void * opaque);
void fio_set_dir_opaque(int dd, void * opaque);
int fio_closedir(int dd);
off_t fio_seekdir(int fd, off_t offset);
ssize_t fio_readdir(int dd, struct dir_entity_t* ent);

int fio_is_open(int fd);
int fio_open(fdread_t, fdwrite_t, fdseek_t, fdclose_t, void * opaque);
ssize_t fio_read(int fd, void * buf, size_t count);
ssize_t fio_write(int fd, const void * buf, size_t count);
off_t fio_seek(int fd, off_t offset, int whence);
int fio_close(int fd);
void fio_set_opaque(int fd, void * opaque);

void register_devfs();

#endif
