#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "threads/thread.h"

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

bool filesys_create_in_dir(const char* name, off_t initial_size, struct dir* dir) {
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  return success;
}

bool filesys_create_dir_in_dir(const char* name, off_t initial_size, struct dir* dir) {
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create_dir(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  return success;
}


/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* input_path) {
  struct thread* ct = thread_current();
  //struct dir* dir = dir_open(ct->cwd == NULL ? dir_open_root() : ct->cwd);
  struct dir* dir;
  char* name = NULL;
  bool success = get_dir_and_name(input_path, &dir, &name);
  if (!success) return NULL;
  struct inode* inode = NULL;

  if (dir != NULL)
    dir_lookup(dir, name, &inode);

  dir_close(dir);

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

  if (path[0] == '/') {
    dir = dir_open_root();
  } else {
    dir = dir_reopen(curr_thread->cwd);
  }

  name = strtok_r(path, "/", &saveptr);
  while (name) {
    dir_found = dir_lookup(dir, name, &inode);
    if (!dir_found)
      return NULL;
    dir_close(dir);
    name = strtok_r(NULL, "/", &saveptr);
    if (name != NULL) {
      if (!inode_is_dir(inode)) { /* if not last, inode must be dir. */
        free(inode);
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

  if (path[0] == '/' || curr_thread->cwd == NULL) {
    *dir = dir_open_root();
  } else {
    *dir = dir_reopen(curr_thread->cwd);
  }

  *name = strtok_r(path, "/", &saveptr);
  while (*name) {
    next_name = strtok_r(NULL, "/", &saveptr);
    dir_found = dir_lookup(*dir, *name, &inode);
    if (!dir_found && next_name == NULL)
      break; // directory does not exist yet
    if (dir_found && next_name == NULL)
      return false; // directory already exists
    if (!dir_found)
      return false; // a directory along the path does not exist
    dir_close(*dir);
    *name = next_name;
    if (*name != NULL) {
      if (!inode_is_dir(inode)) { /* if not last, inode must be dir. */
        free(inode);
        return false;
      } else {
        *dir = dir_open(inode);
      }
    }
  }
  if (*name == NULL) return false;
  temp = malloc(sizeof(char) * strlen(*name) + 1);
  strlcpy(temp, *name, strlen(*name) + 1);
  *name = temp;
  return true;
}