#ifndef __DEVFS_H__
#define __DEVFS_H__

#include <stdio.h>
#include <stdint.h>
#include <filesystem.h>
#include "fio.h"

#define STDIN_HASH 195036193
#define STDOUT_HASH 2141225736
#define STDERR_HASH 2141214883

#define DEVFS_TYPE 164136743

ssize_t stdin_read(struct inode_t* node, void* buf, size_t count, off_t offset);
ssize_t stdout_write(struct inode_t* node, const void* buf, size_t count, off_t offset);

void register_devfs();

int devfs_root_lookup(struct inode_t* node, const char* path);

int devfs_read_inode(inode_t* inode);

int devfs_read_superblock(void* opaque, struct superblock_t* sb);

ssize_t devfs_readdir(struct inode_t* inode, dir_entity_t* ent, off_t offset);
off_t devfs_seek(struct inode_t* node, off_t offset);

//remove those after mounting is working
inode_t* get_stdin_node();
inode_t* get_stdout_node();
inode_t* get_stderr_node();

#endif
