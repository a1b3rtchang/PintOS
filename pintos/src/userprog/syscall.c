#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdlib.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "process.h"
#include "pagedir.h"
#include "devices/shutdown.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer.h"
#include "lib/user/syscall.h"

static struct lock filesys_lock;
static struct lock p_exec_lock;
static void syscall_handler(struct intr_frame*);
void syscall_init(void);
bool correct_args(uint32_t*);
void system_exit(int);
bool byte_checker(void*, struct thread*);
bool str_checker(void*, struct thread*);
bool val_check(void*, struct thread*);
bool pointer_check(void*, struct thread*);
struct file_info* get_file_info(int);
// struct inode* path_to_inode(const char*);
bool mkdir(const char* input_path);
// bool get_dir_and_name(const char*, struct dir**, char**);
bool sys_create(const char*, off_t);
void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
  lock_init(&p_exec_lock);
}

bool byte_checker(void* mem, struct thread* ct) {
  char* byte_check = (char*)mem;
  return is_user_vaddr(&byte_check[0]) && is_user_vaddr(&byte_check[1]) &&
         is_user_vaddr(&byte_check[2]) && is_user_vaddr(&byte_check[3]) &&
         pagedir_get_page(ct->pagedir, &byte_check[0]) != NULL &&
         pagedir_get_page(ct->pagedir, &byte_check[1]) != NULL &&
         pagedir_get_page(ct->pagedir, &byte_check[2]) != NULL &&
         pagedir_get_page(ct->pagedir, &byte_check[3]) != NULL;
}

bool str_checker(void* str, struct thread* ct) {
  char* str_check = (char*)str;
  int i = 0;
  while (is_user_vaddr(&str_check[i]) && pagedir_get_page(ct->pagedir, &str_check[i])) {
    if (str_check[i] == '\0') {
      return true;
    }
    i++;
  }

  return false;
}

bool val_check(void* val, struct thread* ct) {
  return byte_checker(val, ct) && is_user_vaddr(val) &&
         (pagedir_get_page(ct->pagedir, val) != NULL);
}

bool pointer_check(void* val, struct thread* ct) {
  uint32_t* addr = val;
  return byte_checker(val, ct) && val_check(val, ct) && val_check((void*)(*addr), ct);
}

/* argument checker */
bool correct_args(uint32_t* args) {
  struct thread* ct = thread_current();
  if (!byte_checker(&args[0], ct))
    return false;
  switch (args[0]) {
    case SYS_EXIT:
    case SYS_PRACTICE:
    case SYS_WAIT:
    case SYS_CLOSE:
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_ISDIR:
    case SYS_INUMBER:
      return val_check(&args[1], ct);
    case SYS_READDIR: {
      bool ret_val =
          val_check(&args[1], ct) && pointer_check(&args[2], ct);
      if (!ret_val)
        return false;
      /* Checks that buffer args[2] can actually store the size given in args[3] */
      for (size_t i = 0; i < READDIR_MAX_LEN + 1; i++) {
        ret_val = ret_val && is_user_vaddr(&((char*)args[2])[i]) &&
                  pagedir_get_page(ct->pagedir, &((char*)args[2])[i]);
        if (!ret_val) {
          return ret_val;
        }
      }
      return ret_val;
    }
    case SYS_HALT:
      return true;
    case SYS_REMOVE:
    case SYS_EXEC:
    case SYS_OPEN:
    case SYS_CHDIR:
    case SYS_MKDIR:
      return pointer_check(&args[1], ct) && str_checker((void*)args[1], ct);
    case SYS_CREATE:
      return pointer_check(&args[1], ct) && val_check(&args[2], ct) && str_checker(&args[1], ct);
    case SYS_SEEK:
      return val_check(&args[1], ct) && val_check(&args[2], ct);
    case SYS_READ:
    case SYS_WRITE: {
      bool ret_val =
          val_check(&args[1], ct) && pointer_check(&args[2], ct) && val_check(&args[3], ct);
      if (!ret_val)
        return false;
      /* Checks that buffer args[2] can actually store the size given in args[3] */
      for (size_t i = 0; i < (size_t)args[3]; i++) {
        ret_val = ret_val && is_user_vaddr(&((char*)args[2])[i]) &&
                  pagedir_get_page(ct->pagedir, &((char*)args[2])[i]);
        if (!ret_val) {
          return ret_val;
        }
      }
      return ret_val;
    }
  }
  return true;
}

/* frees current thread and associated wait info struct and file descriptors */
void system_exit(int err) {
  struct thread* curr_thread = thread_current();
  struct p_wait_info* parent = curr_thread->parent_pwi;
  struct list* children = &(curr_thread->child_pwis);
  struct p_wait_info* pwi = NULL;
  /* free children pwis */
  while (list_size(children) > 0) {
    pwi = list_entry(list_pop_back(children), struct p_wait_info, elem);
    lock_acquire(&(pwi->access));
    pwi->ref_count--;
    if (pwi->ref_count == 0) {
      free(pwi);
    } else {
      lock_release(&(pwi->access));
    }
  }
  /* free parent pwi */
  if (parent != NULL) {
    lock_acquire(&(parent->access));
    parent->ref_count--;
    if (parent->ref_count == 0) {
      free(parent);
    } else {
      parent->exit_status = err;
      sema_up(&(parent->wait_sem));
      lock_release(&(parent->access));
    }
  }
  curr_thread->user_exit = true;
  printf("%s: exit(%d)\n", thread_current()->name, err);
  thread_exit();
}

/* retrieves file_info struct with file descriptor */
struct file_info* get_file_info(int fd) {
  struct thread* curr_thread = thread_current();
  struct list_elem* iter;
  struct file_info* fi;
  for (iter = list_begin(curr_thread->files); iter != list_end(curr_thread->files);
       iter = list_next(iter)) {
    fi = list_entry(iter, struct file_info, elem);
    if (fi->fd == fd) {
      return fi;
    }
  }
  return NULL;
}

/* PROJECT 3 */
/* Parses path to find inode. 
   returns NULL on failure. 
   Caller must free inode. */
// struct inode* path_to_inode(const char* input_path) {
//   char* saveptr = NULL;
//   char* name;
//   bool dir_found = false;
//   struct inode* inode = NULL;
//   struct thread* curr_thread = thread_current();
//   struct dir* dir;
//   char path[100];
//   strlcpy(path, input_path, sizeof(path));
//   if (path == NULL)
//     return NULL; /* Return if path is NULL */

//   if (path[0] == '/') {
//     dir = dir_open_root();
//   } else {
//     dir = dir_reopen(curr_thread->cwd);
//   }

//   name = strtok_r(path, "/", &saveptr);
//   while (name) {
//     dir_found = dir_lookup(dir, name, &inode);
//     if (!dir_found)
//       return NULL;
//     dir_close(dir);
//     name = strtok_r(NULL, "/", &saveptr);
//     if (name != NULL) {
//       if (!inode_is_dir(inode)) { /* if not last, inode must be dir. */
//         free(inode);
//         return NULL;
//       } else {
//         dir = dir_open(inode);
//       }
//     }
//   }
//   return inode;
// }

// bool get_dir_and_name(const char* input_path, struct dir** dir, char** name) {
//   char* saveptr = NULL;
//   char* next_name;
//   bool dir_found = false;
//   struct inode* inode = NULL;
//   char* temp;
//   struct thread* curr_thread = thread_current();
//   char path[100];
//   strlcpy(path, input_path, sizeof(path));
//   if (path == NULL)
//     return false; /* Return if path is NULL */

//   if (path[0] == '/') {
//     *dir = dir_open_root();
//   } else {
//     *dir = dir_reopen(curr_thread->cwd);
//   }

//   *name = strtok_r(path, "/", &saveptr);
//   while (*name) {
//     next_name = strtok_r(NULL, "/", &saveptr);
//     dir_found = dir_lookup(*dir, *name, &inode);
//     if (!dir_found && next_name == NULL)
//       break; // directory does not exist yet
//     if (dir_found && next_name == NULL)
//       return false; // directory already exists
//     if (!dir_found)
//       return false; // a directory along the path does not exist
//     dir_close(*dir);
//     *name = next_name;
//     if (*name != NULL) {
//       if (!inode_is_dir(inode)) { /* if not last, inode must be dir. */
//         free(inode);
//         return false;
//       } else {
//         *dir = dir_open(inode);
//       }
//     }
//   }
//   if (*name == NULL) return false;
//   temp = malloc(sizeof(char) * strlen(*name) + 1);
//   strlcpy(temp, *name, strlen(*name) + 1);
//   *name = temp;
//   return true;
// }

bool mkdir(const char* input_path) {
  bool success;
  struct dir* dir = NULL;
  char* name = NULL;
  success = get_dir_and_name(input_path, &dir, &name);
  if (success) {
    success = filesys_create_dir_in_dir(name, 0, dir);
    dir_close(dir);
  }
  free(name);
  return success;
}

bool sys_create(const char* input_path, off_t initial_size) {
  bool success;
  struct dir* dir = NULL;
  char* name = NULL;
  success = get_dir_and_name(input_path, &dir, &name);
  if (success) {
    success = filesys_create_in_dir(name, initial_size, dir);
    dir_close(dir);
    free(name);
  }
  return success;
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);
  if (args == NULL || !is_user_vaddr((void*)args) ||
      pagedir_get_page(thread_current()->pagedir, args) == NULL || !correct_args(args)) {
    system_exit(-1);
  }

  switch (args[0]) {
    struct file_info* fi;
    struct inode* inode;
    struct thread* curr_thread;
    case SYS_EXIT:
      f->eax = args[1];
      system_exit(args[1]);
      break;
    case SYS_EXEC:
      lock_acquire(&p_exec_lock);
      f->eax = process_execute((char*)args[1]);
      lock_release(&p_exec_lock);
      break;
    case SYS_PRACTICE:
      f->eax = args[1] + 1;
      break;
    case SYS_HALT:
      buffer_flush(); // Flush the Buffer
      shutdown_power_off();
      break;
    case SYS_WAIT:
      f->eax = process_wait(args[1]);
      break;
    case SYS_WRITE:
      // lock_acquire(&filesys_lock);
      if (args[1] == 0) {
        system_exit(-1);
        break;
      } else if (args[1] == 1) {
        putbuf((char*)args[2], args[3]);
      } else {
        fi = get_file_info(args[1]);
        if (fi) {
          f->eax = file_write(fi->fs, (void*)args[2], args[3]);
        } else {
          f->eax = -1;
        }
        //lock_release(&filesys_lock);
        break;
      }

      //lock_release(&filesys_lock);
      break;
    case SYS_OPEN: {
      //lock_acquire(&filesys_lock);
      struct file* opened_file = filesys_open((char*)args[1]);
      if (!opened_file) {
        f->eax = -1;
      } else {
        fi = malloc(sizeof(struct file_info));
        fi->fd = thread_current()->fd_count;
        thread_current()->fd_count++;
        fi->fs = opened_file;
        list_push_back(thread_current()->files, &(fi->elem));
        f->eax = fi->fd;
      }
      //lock_release(&filesys_lock);
      break;
    }
    case SYS_CLOSE:
      //lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi) {
        file_close(fi->fs);
        list_remove(&fi->elem);
        free(fi);
      } else {
        system_exit(-1);
      }
      //lock_release(&filesys_lock);
      break;
    case SYS_READ:
      //lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi) {
        f->eax = file_read(fi->fs, (void*)args[2], args[3]);
      } else {
        f->eax = -1;
        system_exit(-1);
      }
      //lock_release(&filesys_lock);
      break;
    case SYS_REMOVE:
      //lock_acquire(&filesys_lock);
      f->eax = filesys_remove((char*)args[1]);
      //lock_release(&filesys_lock);
      break;
    case SYS_CREATE:
      //lock_acquire(&filesys_lock);
      f->eax = sys_create((char*)args[1], (off_t)args[2]);
      //lock_release(&filesys_lock);
      break;
    case SYS_TELL:
      //lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi) {
        file_tell(fi->fs);
      } else {
        f->eax = -1;
      }
      //lock_release(&filesys_lock);
      break;
    case SYS_SEEK:
      //lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi) {
        file_seek(fi->fs, args[2]);
      } else {
        system_exit(-1);
      }
      //lock_release(&filesys_lock);
      break;
    case SYS_FILESIZE:
      //lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi) {
        f->eax = file_length(fi->fs);
      } else {
        f->eax = -1;
      }
      //lock_release(&filesys_lock);
      break;
    case SYS_CHDIR:
      curr_thread = thread_current();
      inode = path_to_inode((char*)args[1]);
      if (inode_is_dir(inode)) {
        if (curr_thread->cwd != NULL) {
          dir_close(curr_thread->cwd);
        }
        curr_thread->cwd = dir_open(inode);
        f->eax = true;
      } else {
        f->eax = false;
      }
      break;
    case SYS_MKDIR:
      f->eax = mkdir((char*)args[1]);
      break;
    case SYS_READDIR:
      // error check size of name buffer above
      fi = get_file_info(args[1]);
      if (fi && fi->directory) {
        f->eax = dir_readdir(fi->directory, args[2]);
      } else {
        system_exit(-1);
      }
      break;
    case SYS_ISDIR:
      fi = get_file_info(args[1]);
      if (fi) {
        f->eax = inode_is_dir(file_get_inode(fi->fs));
      } else {
        system_exit(-1);
      }
      break;
    case SYS_INUMBER:
      fi = get_file_info(args[1]);
      if (fi) {
        f->eax = inode_get_inumber(file_get_inode(fi->fs));
      } else {
        system_exit(-1);
      }
      break;
  }
}
