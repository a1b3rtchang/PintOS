#include "devices/block.h"

struct buffer {
  block_sector_t sect_num;
  char data[BLOCK_SECTOR_SIZE];
  struct lock change_data;
  int dirty;
  int valid;
  struct list_elem;
};

void buffer_init(void);
void buffer_read(struct block*, block_sector_t, void*);
void buffer_write(struct block*, block_sector_t, void*);
void buffer_flush(void);
void write_back(void);
void move_to_front(struct list*, struct buffer*);
