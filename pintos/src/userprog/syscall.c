#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

struct lock filesys_lock;
static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

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
      printf("%s: exit(%d)\n", thread_current()->name, args[1]);
      thread_exit();
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
        //system_error_exit(-1);
      } else if (fd == 1) {
        // void putbuf(const char* buffer, size_t n)
        putbuf((char*)args[2], args[3]);
      } else if (fd == 2) {
      }
      lock_release(&filesys_lock);
      break;
    case SYS_CREATE:
      break;
    case SYS_PRACTICE:
      f->eax = args[1] + 1;
      printf("%s: practice(%d)\n", thread_current()->name, args[1]);
      break;
  }
}
