#include "osdebug.h"
#include "filesystem.h"
#include "fio.h"

#include <stdint.h>
#include <string.h>
#include <hash-djb2.h>

#define MAX_FS 16
#define MAX_INODE_CACHE_SIZE 128
#define MAX_FS_DEPTH 16

typedef struct fs_t {
    uint32_t used;
    uint32_t mountpoint;    //in the end of the day, remove this
    superblock_t sb;
    void * opaque;
}fs_t;

static struct fs_t fss[MAX_FS];
static fs_type_t* reg_fss = NULL;

/*
static inode_t* resolvePath(const char* path){
    char** stack[MAX_FS_DEPTH];
    uint32_t i = 0;

    //care Abs Path for now
    if(path[0] != '/')
        return NULL;

    stack[0] = path + 1;
    while(!strchr(stack[i], '/')){
        stack[++i] = strchr(stack[i], '/') + 1;
    }

    inode_t* found;    
    for (i = 0; i < MAX_FS; i++){
        if (fss[i].used && (fss[i].sb.covered == NULL)) {
            fss[i].inop.i_lookup(found, stack[0]);

        }
    }
}
*/

__attribute__((constructor)) void fs_init() {
    memset(fss, 0, sizeof(fss));
    fss[0].used = 1;
    fss[0].mountpoint = hash_djb2((const uint8_t *) "/", -1);
    fss[0].sb.mounted = NULL;
    fss[0].sb.covered = NULL;
}

int register_fs(fs_type_t* type) {
    type->next = reg_fss;
    reg_fss = type; 
    return 0;
}

int fs_mount(const char* mountpoint, uint32_t type, void* opaque){
    uint32_t i;
    uint32_t mount_hash = hash_djb2((const uint8_t *) mountpoint, -1);

    fs_type_t* it = reg_fss;
    fs_t* ptr = NULL;

    for (i = 0; i < MAX_FS; i++) 
        if (!fss[i].used) 
            ptr = fss + i;
    
    for (i = 0; i < MAX_FS; i++) 
        if (fss[i].mountpoint == mount_hash) 
            ptr = NULL;

    if(!ptr)
        return -2;
    
    while(it != NULL){
        if(it->type_name_hash == type){
            if((it->require_dev) && (opaque == NULL))
                return -3;
            ptr->used = 1;
            ptr->mountpoint = mount_hash;
            return it->rsbcb(opaque, &ptr->sb);
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

