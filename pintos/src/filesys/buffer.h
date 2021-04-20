#ifndef FILESYS_BUFFER_H
#define FILESYS_BUFFER_H

#include "devices/block.h"
#include "threads/synch.h"
struct buffer {
  block_sector_t sect_num;
  char data[BLOCK_SECTOR_SIZE];
  struct lock change_data;
  int dirty;
  int valid;
  struct list_elem elem;
};

void buffer_init(void);
void buffer_read(struct block*, block_sector_t, void*);
void buffer_write(struct block*, block_sector_t, void*);
void buffer_flush(void);

#endif
