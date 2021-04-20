#include "filesys/buffer.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

struct buffer cache[64];
struct list* cache_list;
struct lock lru_permission; // dont want threads changing order at the same time

void buffer_init() {
  //initialize lru_permissions lock and each change_data lock
  //initialize cache_list
}

void buffer_read(struct block* block, block_sector_t sect_num, void* buf) {
  lock_acquire(lru_permission);
  for (int i = 0; i < 64 && cache[i] != NULL; i++) {
    if (cache[i].sect_num == sect_num) { // cache hit
      lock_acquire(&cache[i].change_data);
      memcpy(buf, &cache[i].data, BLOCK_SECTOR_SIZE);
      lock_release(&cache[i].change_data);
      move_to_front(cache_list, cache[i]);
      lock_release(lru_permission);
      return;
    }
  }
  // code below handles a cache miss
  struct buffer* b;
  if (list_size(cache_list) == 64) { // evict LRU if cache list is full
    b = list_pop_back(cache_list);
  } else { // create new buffer entry
    b = malloc(sizeof(struct buffer));
    lock_init(&b->change_data);
  }
  if (b->dirty == 1) { // if evicted block has dirty bit, write back to disk
    write_back(b);
  }
  lock_acquire(&b->change_data);
  b->sect_num = sect_num;
  b->dirty = 0;
  block_read(fs_device, sect_num, &b->data); // actually read from disk
  memcpy(buf, &b->data);
  lock_release(&b->change_data);
  list_push_front(cache_list, &b->list_elem);
  lock_release(lru_permission);
}

void buffer_write(struct block* block, block_sector_t sector, void* buf) {
  // same algorithm, except with block_write instead of block_read
  // need to set the dirty bit of the cache entry if we find it in cache
}

void buffer_flush() { // called during filesys_done
  for (int i = 0; i < 64; i++) {
    if (buffer[i].dirty == 1) {
      write_back(buffer[i]); // write the entire
    }
  }
