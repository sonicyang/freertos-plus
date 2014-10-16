#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "devfs.h"
#include "fio.h"
#include "filesystem.h"
#include "osdebug.h"
#include "hash-djb2.h"

static struct fddef_t fio_fds[MAX_FDS];
static struct dddef_t fio_dds[MAX_DDS];

static xSemaphoreHandle fio_sem = NULL;

struct fddef_t * fio_getfd(int fd) {
    if ((fd < 0) || (fd >= MAX_FDS))
        return NULL;
    return fio_fds + fd;
}

static int fio_is_open_int(int fd) {
    if ((fd < 0) || (fd >= MAX_FDS))
        return 0;
    int r = !(fio_fds[fd].inode == NULL);
    return r;
}

static int fio_is_dir_open_int(int dd) {
    if ((dd < 0) || (dd >= MAX_DDS))
        return 0;
    int r = !((fio_dds[dd].ddread == NULL) &&
              (fio_dds[dd].ddclose == NULL) &&
              (fio_dds[dd].opaque == NULL));
    return r;
}

static int fio_findfd() {
    int i;
    
    for (i = 0; i < MAX_FDS; i++) {
        if (!fio_is_open_int(i))
            return i;
    }
    
    return -1;
}

/*
static int fio_finddd() {
    int i;
    
    for (i = 0; i < MAX_DDS; i++) {
        if (!fio_is_dir_open_int(i))
            return i;
    }
    
    return -1;
}
*/

int fio_is_open(int fd) {
    int r = 0;
    xSemaphoreTake(fio_sem, portMAX_DELAY);
    r = fio_is_open_int(fd);
    xSemaphoreGive(fio_sem);
    return r;
}

int fio_open(const char * path, int flags, int mode) {
    int fd, ret, target_node;
    inode_t* p_inode;
    const char* fn = path + strlen(path) - 1;
    char buf[64], fn_buf[128];

    ret = 0;
    while(*fn == '/')fn--, ret++;
    while(*fn != '/')fn--;
    fn++;
    strncpy(fn_buf, fn, strlen(fn) - ret);
    fn_buf[strlen(fn) - ret] = '\0';

    strncpy(buf, path, fn - path);
    buf[fn - path+ 1] = '\0';

//    DBGOUT("fio_open(%p, %p, %p, %p, %p)\r\n", fdread, fdwrite, fdseek, fdclose, opaque);
    ret = get_inode_by_path(buf, &p_inode);
    if(!ret){
        target_node = p_inode->inode_ops.i_lookup(p_inode, fn_buf);

        if(target_node){
            if(p_inode->inode_ops.i_create){
                if(p_inode->inode_ops.i_create(p_inode, fn_buf)){
                    fs_free_inode(p_inode);
                    return -3;       
                }else{
                    target_node = p_inode->inode_ops.i_lookup(p_inode, fn_buf);
                }
            }else{
                fs_free_inode(p_inode);
                return -2;
            }
        }
        
        xSemaphoreTake(fio_sem, portMAX_DELAY);
        fd = fio_findfd();
            
        if (fd >= 0) {
            fio_fds[fd].inode = fs_get_inode(p_inode->device, target_node);
            fio_fds[fd].flags = flags;
            fio_fds[fd].mode = mode;
            fio_fds[fd].opaque = NULL;
        }
        xSemaphoreGive(fio_sem);

        fs_free_inode(p_inode);

        return fd;
    }else{
        return -1;
    }
}

/*
int fio_opendir(ddread_t ddread, ddseek_t ddseek, ddclose_t ddclose, void * opaque) {
    int dd;
//    DBGOUT("fio_open(%p, %p, %p, %p, %p)\r\n", fdread, fdwrite, fdseek, fdclose, opaque);
    xSemaphoreTake(fio_sem, portMAX_DELAY);
    dd = fio_finddd();
    
    if (dd >= 0) {
        fio_dds[dd].ddread = ddread;
        fio_dds[dd].ddseek = ddseek;
        fio_dds[dd].ddclose = ddclose;
        fio_dds[dd].opaque = opaque;
    }
    xSemaphoreGive(fio_sem);
    
    return dd;
}
*/

ssize_t fio_read(int fd, void * buf, size_t count) {
    ssize_t r = 0;
//    DBGOUT("fio_read(%i, %p, %i)\r\n", fd, buf, count);
    if (fio_is_open_int(fd)) {
        if (fio_fds[fd].inode->file_ops.read) {
            xSemaphoreTake(fio_fds[fd].inode->lock, portMAX_DELAY);
            r = fio_fds[fd].inode->file_ops.read(fio_fds[fd].inode, buf, count, fio_fds[fd].cursor);
            fio_fds[fd].cursor += r;
            xSemaphoreGive(fio_fds[fd].inode->lock);
        }
    } else {
        r = -2;
    }
    return r;
}

/*
ssize_t fio_readdir(int dd, struct dir_entity_t* ent) {
    ssize_t r = 0;
//    DBGOUT("fio_read(%i, %p, %i)\r\n", fd, buf, count);
    if (fio_is_dir_open_int(dd)) {
        if (fio_dds[dd].ddread) {
            r = fio_dds[dd].ddread(fio_dds[dd].opaque, ent);
        } else {
            r = -3;
        }
    } else {
        r = -2;
    }
    return r;
}
*/

ssize_t fio_write(int fd, const void * buf, size_t count) {
    ssize_t r = 0;
//    DBGOUT("fio_write(%i, %p, %i)\r\n", fd, buf, count);
    if (fio_is_open_int(fd)) {
        if (fio_fds[fd].inode->file_ops.write) {
            xSemaphoreTake(fio_fds[fd].inode->lock, portMAX_DELAY);
            r = fio_fds[fd].inode->file_ops.write(fio_fds[fd].inode, buf, count, fio_fds[fd].cursor);
            fio_fds[fd].cursor += r;
            xSemaphoreGive(fio_fds[fd].inode->lock);
        } else {
            r = -3;
        }
    } else {
        r = -2;
    }
    return r;
}

off_t fio_seek(int fd, off_t offset, int whence) {
//    DBGOUT("fio_seek(%i, %i, %i)\r\n", fd, offset, whence);
    if (fio_is_open_int(fd)) {
        
        uint32_t size = fio_fds[fd].inode->size;
        uint32_t origin;
        
        switch (whence) {
        case SEEK_SET:
            origin = 0;
            break;
        case SEEK_CUR:
            origin = fio_fds[fd].cursor;
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

        xSemaphoreTake(fio_sem, portMAX_DELAY);
        fio_fds[fd].cursor = offset;
        xSemaphoreGive(fio_sem);
        return offset;
    } else {
        return -2;
    }
    return -3;
}

/*
off_t fio_seekdir(int dd, off_t offset) {
    off_t r = 0;
//    DBGOUT("fio_seek(%i, %i, %i)\r\n", dd, offset, whence);
    if (fio_is_open_int(dd)) {
        if (fio_dds[dd].ddseek) {
            r = fio_dds[dd].ddseek(fio_dds[dd].opaque, offset);
        } else {
            r = -3;
        }
    } else {
        r = -2;
    }
    return r;
}
*/

int fio_close(int fd) {
    int r = 0;
//    DBGOUT("fio_close(%i)\r\n", fd);
    if (fio_is_open_int(fd)) {
//        if (fio_fds[fd].fdclose)
  //          r = fio_fds[fd].fdclose(fio_fds[fd].opaque);
        xSemaphoreTake(fio_sem, portMAX_DELAY);
        fs_free_inode(fio_fds[fd].inode);
        memset(fio_fds + fd, 0, sizeof(struct fddef_t));
        xSemaphoreGive(fio_sem);
    } else {
        r = -2;
    }
    return r;
}

/*
int fio_closedir(int dd) {
    int r = 0;
//    DBGOUT("fio_close(%i)\r\n", fd);
    if (fio_is_dir_open_int(dd)) {
        if (fio_dds[dd].ddclose)
            r = fio_dds[dd].ddclose(fio_dds[dd].opaque);
        xSemaphoreTake(fio_sem, portMAX_DELAY);
        memset(fio_dds + dd, 0, sizeof(struct dddef_t));
        xSemaphoreGive(fio_sem);
    } else {
        r = -2;
    }
    return r;
}
*/

void fio_set_opaque(int fd, void * opaque) {
    if (fio_is_open_int(fd))
        fio_fds[fd].opaque = opaque;
}

void fio_set_dir_opaque(int dd, void * opaque) {
    if (fio_is_dir_open_int(dd))
        fio_dds[dd].opaque = opaque;
}


/*
static int devfs_open(void * opaque, const char * path, int flags, int mode) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
//    DBGOUT("devfs_open(%p, \"%s\", %i, %i)\r\n", opaque, path, flags, mode);
    switch (h) {
    case stdin_hash:
        if (flags & (O_WRONLY | O_RDWR))
            return -1;
        return fio_open(stdin_read, NULL, NULL, NULL, NULL);
        break;
    case stdout_hash:
        if (flags & O_RDONLY)
            return -1;
        return fio_open(NULL, stdout_write, NULL, NULL, NULL);
        break;
    case stderr_hash:
        if (flags & O_RDONLY)
            return -1;
        return fio_open(NULL, stdout_write, NULL, NULL, NULL);
        break;
    }
    return -1;
}
*/

__attribute__((constructor)) void fio_init() {
    memset(fio_fds, 0, sizeof(fio_fds));
    fio_fds[0].inode = get_stdin_node();
    fio_fds[0].inode->lock = xSemaphoreCreateMutex();
    fio_fds[1].inode = get_stdout_node();
    fio_fds[1].inode->lock = xSemaphoreCreateMutex();
    fio_fds[2].inode = get_stderr_node();
    fio_fds[2].inode->lock = xSemaphoreCreateMutex();
    fio_sem = xSemaphoreCreateMutex();
}

