#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block* fs_device;

void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char* name, off_t initial_size);
struct file* filesys_open(const char* name);
bool filesys_remove(const char* name);
bool filesys_create_in_dir(const char*, off_t);
bool filesys_create_dir_in_dir(const char*, off_t);
struct inode* path_to_inode(const char*);
bool get_dir_and_name(const char*, struct dir**,
                      char**); // find file in dir given name and directory

#endif /* filesys/filesys.h */
