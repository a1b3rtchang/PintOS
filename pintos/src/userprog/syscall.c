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

static struct lock filesys_lock;
static struct lock p_exec_lock;
static void syscall_handler(struct intr_frame*);
void syscall_init(void);
bool correct_args(uint32_t*);
void system_exit(int);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
  lock_init(&p_exec_lock);
}

bool correct_args(uint32_t* args) {
  switch (args[0]) {
    case SYS_EXIT:
    case SYS_PRACTICE:
    case SYS_WAIT:
      return is_user_vaddr(&args[1]); /* Check if input is stored in valid memory */
    case SYS_EXEC:
      return is_user_vaddr(&args[1]) && is_user_vaddr((void *)args[1]) &&
             pagedir_get_page(thread_current()->pagedir, &args[1]) != NULL &&
             pagedir_get_page(thread_current()->pagedir, (void *) args[1]) !=
                 NULL; /* Check if location of char* is valid AND if where cha * is pointing to is also valid */
    case SYS_HALT:
      return true;
    case SYS_WRITE:
      is_user_vaddr((void *) &args[2]) && is_user_vaddr((void *) args[3]);

  }
  return true;
}

// frees current thread and associated wait info struct
void system_exit(int err) {
  struct thread* curr_thread = thread_current();
  struct p_wait_info* parent = curr_thread->parent_pwi;
  // struct list* children = &(curr_thread->child_pwis);
  // struct list_elem* iter;
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

  printf("%s: exit(%d)\n", thread_current()->name, err);
  thread_exit();
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
    case SYS_EXIT:
      f->eax = args[1];
      system_exit(args[1]);
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
      int fd = args[1];
      if (fd == 0) {
        system_exit(-1);
      } else if (fd == 1) {
        // void putbuf(const char* buffer, size_t n)
        // 100bytes. reasonable to break it down.
        putbuf((char*)args[2], args[3]);
      } else if (fd == 2) {
        
      } else{
        /*if ((thread_current -> files) == NULL){
            struct file_info* new_file;
            new_file -> fd = 3;
            filesys_create(args[2], args[3]); 
            new_file -> file = filesys_open(args[2]); 
            list_puch_back(thread_current() -> files, &(new_file -> elem))
            return file_write(new_file.file, (void *) args[2], args[3])
        } else {
            struct list_elem* last_elem = list_back(thread_current() -> files);
            struct file_info* current_file = list_entry(last_elem, file_info, elem); 
            struct file_info* new_file;
            new_file -> fd = (current_file -> fd) + 1;
            filesys_create(args[2], args[3]); 
            new_file -> file = filesys_open(args[2]); 
            list_push_back(thread_current() -> files, &(newfile -> elem))
            return file_write(new_file.file, (void *) args[2], args[3])
            
      
        }
        // thread_current() -> file_list
        */
      }
      lock_release(&filesys_lock);
      break;
    case SYS_CREATE:
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
  }
}
