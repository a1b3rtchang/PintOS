#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdlib.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "process.h"
#include "pagedir.h"
#include "devices/shutdown.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static struct lock filesys_lock;
static struct lock p_exec_lock;
static void syscall_handler(struct intr_frame*);
void syscall_init(void);
bool correct_args(uint32_t*);
void system_exit(int);
bool byte_checker(void*, struct thread*);
bool str_checker(void*, struct thread*);
struct file_info* get_file_info(int);

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
  int i;
  for (i = 0; str_check[i] != '\0'; i++) {
    if (!(is_user_vaddr(&str_check[i]) && pagedir_get_page(ct->pagedir, &str_check[i]))) {
      return false;
    }
  }
  return is_user_vaddr(&str_check[i]) && pagedir_get_page(ct->pagedir, &str_check[i]);
}

bool correct_args(uint32_t* args) {
  struct thread* ct = thread_current();
  if (!byte_checker(&args[0], ct))
    return false;
  switch (args[0]) {
    case SYS_EXIT:
    case SYS_PRACTICE:
    case SYS_WAIT:
      return is_user_vaddr(&args[1]) && pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             byte_checker(&args[1], ct); /* Check if input is stored in valid memory */
    case SYS_EXEC:
      return is_user_vaddr(&args[1]) && is_user_vaddr((void*)args[1]) &&
             pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             pagedir_get_page(ct->pagedir, (void*)args[1]) != NULL &&
             str_checker(
                 &args[1],
                 ct); /* Check if location of char* is valid AND if where cha * is pointing to is also valid */
    case SYS_HALT:
      return true;
    case SYS_REMOVE:
    case SYS_OPEN:
      return is_user_vaddr(&args[1]) && is_user_vaddr((void*)args[1]) &&
             pagedir_get_page(ct->pagedir, &args[1]) &&
             pagedir_get_page(ct->pagedir, (void*)args[1]) && str_checker(&args[1], ct);
    case SYS_CREATE:
      return is_user_vaddr(&args[1]) && is_user_vaddr((void*)args[1]) &&
             pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             pagedir_get_page(ct->pagedir, (void*)args[1]) != NULL && is_user_vaddr(&args[2]) &&
             pagedir_get_page(ct->pagedir, &args[2]) != NULL && is_user_vaddr((void*)args[2]) &&
             str_checker(&args[1], ct) && byte_checker(&args[2], ct) && (off_t)args[2] >= 0;
    case SYS_TELL:
      return is_user_vaddr(&args[1]) && is_user_vaddr((void*)args[1]) &&
             pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             pagedir_get_page(ct->pagedir, (void*)args[1]) != NULL && byte_checker(&args[1], ct);
    case SYS_SEEK:
      return is_user_vaddr(&args[1]) && is_user_vaddr((void*)args[1]) &&
             pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             pagedir_get_page(ct->pagedir, (void*)args[1]) != NULL && is_user_vaddr(&args[2]) &&
             pagedir_get_page(ct->pagedir, &args[2]) != NULL && byte_checker(&args[1], ct) &&
             byte_checker(&args[2], ct) && (off_t)args[2] >= 0;

    case SYS_CLOSE:
      return is_user_vaddr(&args[1]) && pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             byte_checker(&args[1], ct);
    case SYS_FILESIZE:
      return is_user_vaddr(&args[1]) && pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
             byte_checker(&args[1], ct);
    case SYS_READ:
      return byte_checker(&args[1], ct) && byte_checker(&args[2], ct) &&
             is_user_vaddr((void*)args[2]);
    case SYS_WRITE: {
      bool ret_val = is_user_vaddr(&args[1]) && is_user_vaddr(&args[2]) &&
                     is_user_vaddr(&args[3]) && pagedir_get_page(ct->pagedir, &args[1]) != NULL &&
                     pagedir_get_page(ct->pagedir, &args[2]) != NULL &&
                     pagedir_get_page(ct->pagedir, &args[3]) != NULL &&
                     byte_checker(&args[1], ct) && byte_checker(&args[3], ct);
      if (!ret_val)
        return false;

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

// frees current thread and associated wait info struct
void system_exit(int err) {
  struct thread* curr_thread = thread_current();
  struct p_wait_info* parent = curr_thread->parent_pwi;
  // struct list* children = &(curr_thread->child_pwis);
  // struct list_elem* iter;
  struct list* children = &(curr_thread->child_pwis);
  struct p_wait_info* pwi = NULL;

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

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);
  if (args == NULL || !is_user_vaddr((void*)args) ||
      pagedir_get_page(thread_current()->pagedir, args) == NULL || !correct_args(args)) {
    system_exit(-1);
  }
  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  switch (args[0]) {
    struct file_info* fi;
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
      // TODO: free files + pwaitinfos + other
      shutdown_power_off();
      break;
    case SYS_WAIT:
      f->eax = process_wait(args[1]);
      break;
    case SYS_WRITE:
      //int write(int fd, const void* buffer, unsigned size) {
      //return syscall3(SYS_WRITE, fd, buffer, size);

      //input:(int fd, const void *buffer, unsigned size)
      //output:
      //off_t file_write(struct file* file, const void* buffer, off_t size) from file.c
      //buffer is the string that is being written in
      // args[1] = file descriptor
      // args[2] = pointer to buffer
      // args[3] = the max size we want
      lock_acquire(&filesys_lock);
      if (args[1] == 0) {
        system_exit(-1);
        break;
      } else if (args[1] == 1) {
        // void putbuf(const char* buffer, size_t n)
        // 100bytes. reasonable to break it down.
        putbuf((char*)args[2], args[3]);
      } else {
        struct file_info* get_file = get_file_info(args[1]);
        if (get_file == NULL) {
          f->eax = -1;
          lock_release(&filesys_lock);
          break;
        }
        f->eax = file_write(get_file->fs, (void*)args[2], args[3]);
        lock_release(&filesys_lock);
        break;

        /*if ((thread_current -> files) == NULL){
            struct file_info* new_file;
            new_file -> fd = 3;
            filesys_create(args[2], args[3]);
            new_file -> file = filesys_open(args[2]);
            list_puch_back(thread_current() -> files, &(new_file -> elem))
            return file_write(new_file.file, (void *) args[2], args[3])
        } else {
          /*make
            struct list_elem* last_elem = list_back(thread_current() -> files);
            struct file_info* current_file = list_entry(last_elem, file_info, elem);
            struct file_info* new_file;
            new_file -> fd = (current_file -> fd) + 1;
            filesys_create(args[2], args[3]);
            new_file -> file = filesys_open(args[2]);
            list_push_back(thread_current() -> files, &(newfile -> elem));
            return file_write(new_file.file, (void *) args[2], args[3]);
            break;
            */
      }

      lock_release(&filesys_lock);
      break;
    case SYS_OPEN: {
      lock_acquire(&filesys_lock);
      struct file* opened_file = filesys_open((char*)args[1]);
      if (opened_file == NULL) {
        f->eax = -1;
        lock_release(&filesys_lock);
        break;
      }
      fi = malloc(sizeof(struct file_info));
      fi->fd = thread_current()->fd_count;
      thread_current()->fd_count++;
      fi->fs = opened_file;
      list_push_back(thread_current()->files, &(fi->elem));
      f->eax = fi->fd;
      lock_release(&filesys_lock);
      break;
    }
    case SYS_CLOSE:
      fi = get_file_info(args[1]);
      if (fi == NULL) {
        system_exit(-1);
      } else {
        file_close(fi->fs);
        list_remove(&fi->elem);
        free(fi);
      }
      break;
    case SYS_READ:
      fi = get_file_info(args[1]);
      if (fi == NULL) {
        f->eax = -1;
        system_exit(-1);
      } else {
        f->eax = file_read(fi->fs, (void*)args[2], args[3]);
      }
      break;
    case SYS_REMOVE:
      lock_acquire(&filesys_lock);
      f->eax = filesys_remove((char*)args[1]);
      lock_release(&filesys_lock);
      break;
    case SYS_CREATE:
      lock_acquire(&filesys_lock);
      f->eax = filesys_create((char*)args[1], (off_t)args[2]);
      lock_release(&filesys_lock);
      break;
    case SYS_TELL:
      lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi != NULL) {
        file_tell(fi->fs);
      } else {
        f->eax = -1;
      }
      lock_release(&filesys_lock);
      break;
    case SYS_SEEK:
      lock_acquire(&filesys_lock);
      fi = get_file_info(args[1]);
      if (fi != NULL) {
        file_seek(fi->fs, args[2]);
      } else {
        system_exit(-1);
      }
      lock_release(&filesys_lock);
      break;
  }
}
