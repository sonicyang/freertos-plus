#include "osdebug.h"
#include "filesystem.h"
#include "fio.h"

#include <stdint.h>
#include <string.h>
#include <hash-djb2.h>

#define MAX_FS 16
#define MAX_INODE_CACHE_SIZE 8
#define MAX_FS_DEPTH 16

typedef struct fs_t {
    uint32_t used;
    superblock_t sb;
    void * opaque;
}fs_t;

static struct fs_t fss[MAX_FS];
static fs_type_t* reg_fss = NULL;
static inode_t inode_pool[MAX_INODE_CACHE_SIZE];

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
    memset(inode_pool, 0, sizeof(inode_pool));
}

int register_fs(fs_type_t* type) {
    type->next = reg_fss;
    reg_fss = type; 
    return 0;
}

int fs_mount(inode_t* mountpoint, uint32_t type, void* opaque){
    uint32_t i;

    fs_type_t* it = reg_fss;
    fs_t* ptr = NULL;

    for (i = 0; i < MAX_FS; i++) 
        if (!fss[i].used){ 
            ptr = fss + i;
            break;
        }
    
    for (i = 0; i < MAX_FS; i++) 
        if ((fss[i].sb.covered == mountpoint) && (mountpoint != NULL)){ 
            ptr = NULL;
            break;
        }

    if(!ptr)
        return -2;
    
    while(it != NULL){
        if(it->type_name_hash == type){
            if((it->require_dev) && (opaque == NULL))
                return -3;
            ptr->used = 1;
            ptr->sb.covered = mountpoint;
            if(mountpoint){
                mountpoint->mode |= 2; //set mountpoint as covered
                mountpoint->count++;
            }
            return it->rsbcb(opaque, &ptr->sb);
        }
        it = it->next;
    } 
    return -1;
}

inode_t* fs_open_inode(uint32_t device, uint32_t number){
    for(uint32_t i = 0; i < MAX_INODE_CACHE_SIZE; i++){
        if((inode_pool[i].device == device) && (inode_pool[i].number == number)){
            inode_pool[i].count++;
            return inode_pool + i;
        }
    }

    for(uint32_t i = 0; i < MAX_FS; i++){
        if((fss[i].used) && (fss[i].sb.device == device)){
            for(uint32_t j = 0; j < MAX_INODE_CACHE_SIZE; j++){
                if(inode_pool[j].count == 0){
                    inode_pool[j].device = device;
                    inode_pool[j].number = number;
                    if(fss[i].sb.superblock_ops.s_read_inode(&inode_pool[j])){
                        inode_pool[j].device = 0;
                        inode_pool[j].number = 0;
                        return NULL;
                    }
                    inode_pool[j].count++;
                    if(inode_pool[j].lock == NULL)
                        inode_pool[j].lock = xSemaphoreCreateMutex();
                    return inode_pool + j;
                }
            }
        }
    }

    return NULL;
}

void fs_close_inode(inode_t* inode){
    //Should i_ops close 
    inode->count--;
    return;
}

int fs_open(const char* path, inode_t** inode){
    inode_t *ptr, *ptr2;
    int32_t ret;

    const char * slash = path;

    for(uint32_t i = 0; i < MAX_FS; i++){
        if((fss[i].used) && (fss[i].sb.covered == NULL)){
            ptr = fs_open_inode(fss[i].sb.device, fss[i].sb.mounted);
            break;
        }
    }
    
    slash = path;
    while(1){
        slash = strchr(slash, '/');
        if(!slash || slash[1] == '\0')
            break;
        slash++;
        
        if(ptr->mode & 0x0002){
            for(uint32_t i = 0; i < MAX_FS; i++){
                if((fss[i].used) && (fss[i].sb.covered == ptr)){
                    ptr2 = ptr;
                    ptr = fs_open_inode(fss[i].sb.device, fss[i].sb.mounted);
                    fs_close_inode(ptr2);
                }
            }
        }else{
            ptr2 = ptr;
            ret = ptr->inode_ops.i_lookup(ptr, slash);
            if(ret < 0){
                *inode = NULL;
                fs_close_inode(ptr);
                return -1;
            }
            ptr = fs_open_inode(ptr->device, ret);
            fs_close_inode(ptr2);
        }
    }
    
    *inode = ptr;
    return 0; 
}

int fs_mkdir(const char * path) {
    int ret, target_node;
    inode_t* p_inode;
    const char* fn = path + strlen(path) - 1;
    char buf[64], fn_buf[128];

    if(strcmp(path, "/") == 0)
        return -1;

    ret = 0;
    while(*fn == '/')fn--, ret++;
    while(*fn != '/')fn--;
    fn++;
    strncpy(fn_buf, fn, strlen(fn) - ret);
    fn_buf[strlen(fn) - ret] = '\0';

    strncpy(buf, path, fn - path);
    buf[fn - path] = '\0';

//    DBGOUT("fio_open(%p, %p, %p, %p, %p)\r\n", fdread, fdwrite, fdseek, fdclose, opaque);
    ret = fs_open(buf, &p_inode);
    if(!ret){
        target_node = p_inode->inode_ops.i_lookup(p_inode, fn_buf);

        if(!target_node){
            return -1;
        }else{
            if(p_inode->inode_ops.i_mkdir){
                xSemaphoreTake(p_inode->lock, portMAX_DELAY);
                if(p_inode->inode_ops.i_mkdir(p_inode, fn_buf)){
                    xSemaphoreGive(p_inode->lock);
                    fs_close_inode(p_inode);
                    return -3;       
                }else{
                    xSemaphoreGive(p_inode->lock);
                    fs_close_inode(p_inode);
                    return 0;       
                }
            }else{
                fs_close_inode(p_inode);
                return -2;
            }
        }
    }else{
        return -1;
    }
}

/*
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
*/
