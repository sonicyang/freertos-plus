#include "osdebug.h"
#include "filesystem.h"
#include "fio.h"

#include <stdint.h>
#include <string.h>
#include <hash-djb2.h>

#define MAX_FS 16
#define MAX_INODE_CACHE_SIZE

typedef struct inode_t{
    uint32_t device;
    uint32_t number;
    uint32_t mode;
    uint32_t block_size;
    uint32_t count;
    uint32_t lock;
    void* opaque;
}inode_t;

typedef struct superblock_t{
    uint32_t device;
    inode_t inode_list[MAX_INODE_CACHE_SIZE];
    uint32_t block_size;
    uint32_t type_hash;
    void* opaque;
}superblock_t;

typedef struct superblock_operations{
    uint32_t inode_count;
    uint32_t block_count;
    ramfs_inode_t** inode_list;
    ramfs_block_t** block_pool;
}superblock_operations;

typedef struct fs_type_t {
    uint32_t type_name_hash;
    fs_read_superblock_callback_t rsbcb;
    uint32_t require_dev; 
    fs_type_t* next;
}fs_type_t;

typedef struct fs_t {
    uint32_t used;
    uint32_t mountpoint;    //in the end of the day, remove this
    superblock_t sb;
    superblock_operations sbop;
    inode_operations inop;
    void * opaque;
}fs_t;

static struct fs_t fss[MAX_FS];
static fs_type_t* reg_fss = NULL;

__attribute__((constructor)) void fs_init() {
    memset(fss, 0, sizeof(fss));
}

int register_fs(fs_type_t* type) {
    type->next = reg_fss;
    reg_fss = type; 
    return 0;
}

int mount_fs(uint32_t mountpoint, uint32_t type, void* opaque){
    fs_type_t* it = reg_fss;
    fs_t* ptr = NULL;

    for (i = 0; i < MAX_FS; i++) 
        if (!fss[i].used) 
            ptr = fss + i;
    
    if(!ptr)
        return -2;
    
    while(it != NULL){
        if(it->type_name_hash == type){
            if((it->require_dev) && (opaque == NULL))
                return -3;
            ptr->used = 1;
            ptr->mountpoint = mountpoint;
            return it->rsbcb(opaque, ptr);
        }
    } 
    return -1;
}

int fs_open(const char * path, int flags, int mode) {
    const char * slash;
    uint32_t hash;
    int i;
//    DBGOUT("fs_open(\"%s\", %i, %i)\r\n", path, flags, mode);
    
    while (path[0] == '/')
        path++;
    
    slash = strchr(path, '/');
    
    if (!slash)
        return -2;

    hash = hash_djb2((const uint8_t *) path, slash - path);
    path = slash + 1;

    for (i = 0; i < MAX_FS; i++) {
        if (fss[i].hash == hash)
            return fss[i].cb(fss[i].opaque, path, flags, mode);
    }
    
    return -2;
}

int fs_opendir(char* path) {
    char * slash;
    uint32_t hash;
    int i;
//    DBGOUT("fs_open(\"%s\", %i, %i)\r\n", path, flags, mode);
    
    while (path[0] == '/')
        path++;
    
    slash = strchr(path, '/');
    
    if (!slash)
        return -2;

    hash = hash_djb2((const uint8_t *) path, slash - path);
    path = slash + 1;

    for (i = 0; i < MAX_FS; i++) {
        if (fss[i].hash == hash)
            return fss[i].od_cb(fss[i].opaque, path);
    }
    
    return -2;
}

