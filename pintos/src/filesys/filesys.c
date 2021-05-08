#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include <stdlib.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/synch.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) { free_map_close(); }

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  block_sector_t inode_sector = 0;
  struct thread* ct = thread_current();
  struct dir* dir = (ct->cwd == NULL ? dir_open_root() : ct->cwd);
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  if (ct->cwd == NULL) // only close if not cwd
    dir_close(dir);
  return success;
}

bool filesys_create_in_dir(const char* input_path, off_t initial_size) {
  block_sector_t inode_sector = 0;
  struct dir* dir = NULL;
  char* name = NULL;
  bool success = get_dir_and_name(input_path, &dir, &name);
  if (!success)
    return false;
  success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
             inode_create(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  free(name);
  dir_close(dir);
  return success;
}

bool filesys_create_dir_in_dir(const char* input_path, off_t initial_size) {
  block_sector_t inode_sector = 0;
  struct dir* dir = NULL;
  char* name = NULL;
  struct inode* dummy = NULL;
  bool success = get_dir_and_name(input_path, &dir, &name);
  if (!success)
    return false;
  success = !dir_lookup(dir, name, &dummy);
  if (!success) {
    free(dummy);
    dir_close(dir);
    free(name);
    return false;
  }
  success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
             inode_create_dir(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  free(name);
  dummy = path_to_inode(input_path);
  struct dir* child = dir_open(dummy);
  dir_add(child, "..", inode_get_inumber(dir_get_inode(dir)));
  dir_close(dir);
  dir_close(child);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* input_path) {
  struct inode* inode = path_to_inode(input_path);
  return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* input_path) {
  struct dir* dir;
  char* name = NULL;
  bool success = get_dir_and_name(input_path, &dir, &name);
  if (success) {
    success = dir != NULL && dir_remove(dir, name);
    dir_close(dir);
  }
  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}

struct inode* path_to_inode(const char* input_path) {
  char* saveptr = NULL;
  char* name;
  bool dir_found = false;
  struct inode* inode = NULL;
  struct thread* curr_thread = thread_current();
  struct dir* dir;
  char path[100];
  strlcpy(path, input_path, sizeof(path));
  if (path == NULL)
    return NULL; /* Return if path is NULL */

  if (strcmp(path, "/") == 0) {
    dir = dir_open_root();
    inode = dir_get_inode(dir);
    dir_close(dir);
    return inode;
  }

  if (path[0] == '/' || curr_thread->cwd == NULL) {
    dir = dir_open_root();
  } else if (!inode_removed(dir_get_inode(curr_thread->cwd))) {
    dir = dir_reopen(curr_thread->cwd);
  } else {
    return NULL;
  }

  name = strtok_r(path, "/", &saveptr);
  while (name) {
    dir_found = dir_lookup(dir, name, &inode);
    dir_close(dir);
    if (!dir_found)
      return NULL;
    name = strtok_r(NULL, "/", &saveptr);
    if (name != NULL) {
      if (!inode_is_dir(inode)) { /* if not last, inode must be dir. */
        inode_close(inode);
        return NULL;
      } else {
        dir = dir_open(inode);
      }
    }
  }
  return inode;
}

bool get_dir_and_name(const char* input_path, struct dir** dir, char** name) {
  char* saveptr = NULL;
  char* next_name;
  bool dir_found = false;
  struct inode* inode = NULL;
  char* temp;
  struct thread* curr_thread = thread_current();
  char path[100];
  strlcpy(path, input_path, sizeof(path));
  if (path == NULL)
    return false; /* Return if path is NULL */

  if (path[0] == '/' || curr_thread->cwd == NULL) { // || curr_thread->cwd == NULL) {
    *dir = dir_open_root();
  } else if (!inode_removed(dir_get_inode(curr_thread->cwd))) {
    *dir = dir_reopen(curr_thread->cwd);
  } else {
    return false;
  }

  *name = strtok_r(path, "/", &saveptr);
  while (*name) {
    next_name = strtok_r(NULL, "/", &saveptr);
    if (next_name == NULL)
      break; // directory does not exist yet
    dir_found = dir_lookup(*dir, *name, &inode);
    dir_close(*dir);
    if (!dir_found)
      return false; // a directory along the path does not exist
    *name = next_name;
    if (*name != NULL) {
      if (!inode_is_dir(inode)) { /* if not last, inode must be dir. */
        inode_close(inode);
        return false;
      } else {
        *dir = dir_open(inode);
      }
    }
  }
  if (*name == NULL)
    return false;
  /* don't close inode because inode is for dir */
  temp = malloc(sizeof(char) * strlen(*name) + 1);
  strlcpy(temp, *name, strlen(*name) + 1);
  *name = temp;
  return true;
}
