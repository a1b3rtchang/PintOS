#include "filesys/buffer.h"
#include <stddef.h>
#include <stdio.h>
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static struct buffer cache[64];
static struct list cache_list;
static struct lock lru_permission; // dont want threads changing order at the same time
void write_back(struct buffer*);
void acquire_entry(block_sector_t);

void buffer_init() {
  for (int i = 0; i < 64; i++) {
    cache[i].valid = 0;
    cache[i].dirty = 0;
    lock_init(&cache[i].change_data);
  }
  list_init(&cache_list);
  list_size(&cache_list);
  lock_init(&lru_permission);
}

void write_back(struct buffer* b) {
  block_write(fs_device, b->sect_num, &b->data);
  b->dirty = 0;
}

void acquire_entry(block_sector_t sect_num) { return; }

void buffer_read(struct block* block, block_sector_t sect_num, void* buf) {
  lock_acquire(&lru_permission);
  bool again = true;
  while (again) {
    again = false;
    for (int i = 0; i < 64; i++) {
      if (cache[i].sect_num == sect_num && cache[i].valid == 1) { // cache hit
        lock_release(&lru_permission);

        lock_acquire(&cache[i].change_data);
        if (cache[i].sect_num == sect_num) {
          memcpy(buf, &cache[i].data, BLOCK_SECTOR_SIZE);
          lock_release(&cache[i].change_data);
          /*move to front*/
          lock_acquire(&lru_permission);
          list_remove(&cache[i].elem);
          list_push_front(&cache_list, &cache[i].elem);
          lock_release(&lru_permission);
          return;
        }
        lock_release(&cache[i].change_data);
        lock_acquire(&lru_permission);
        again = true; /*try again */
      }
    }
  }
  // code below handles a cache miss
  /* if hair pulling occurs, remove locks below for blocks */
  struct buffer* b;
  if (list_size(&cache_list) == 64) { // evict LRU if cache list is full
    b = list_entry(list_pop_back(&cache_list), struct buffer, elem);
    lock_acquire(&b->change_data);
  } else { // create new buffer entry
    for (int i = 0; i < 64; i++) {
      b = &cache[i];
      lock_acquire(&b->change_data);
      if (b->valid == 0) {
        break;
      }
      lock_release(&b->change_data);
    }
  }
  if (b->dirty == 1) { // if evicted block has dirty bit, write back to disk
    write_back(b);
  }
  b->dirty = 0;
  b->valid = 1;
  b->sect_num = sect_num;
  block_read(fs_device, sect_num, &b->data); // actually read from disk
  memcpy(buf, &b->data, BLOCK_SECTOR_SIZE);
  lock_release(&b->change_data);
  list_push_front(&cache_list, &b->elem);
  lock_release(&lru_permission);
}

void buffer_write(struct block* block, block_sector_t sect_num, void* buf) {
  // same algorithm, except with block_write instead of block_read
  // need to set the dirty bit of the cache entry if we find it in cache
  lock_acquire(&lru_permission);
  bool again = true;
  while (again) {
    again = false;
    for (int i = 0; i < 64; i++) {
      if (cache[i].sect_num == sect_num && cache[i].valid == 1) { // cache hit
        lock_release(&lru_permission);

        lock_acquire(&cache[i].change_data);
        if (cache[i].sect_num == sect_num) {
          memcpy(cache[i].data, buf, BLOCK_SECTOR_SIZE);
          cache[i].dirty = true;
          lock_release(&cache[i].change_data);
          /*move to front*/
          lock_acquire(&lru_permission);
          list_remove(&cache[i].elem);
          list_push_front(&cache_list, &cache[i].elem);
          lock_release(&lru_permission);
          return;
        }
        lock_release(&cache[i].change_data);
        lock_acquire(&lru_permission);
        again = true; /*try again */
      }
    }
  }
  // code below handles a cache miss
  /* if hair pulling occurs, remove locks below for blocks */
  struct buffer* b;
  if (list_size(&cache_list) == 64) { // evict LRU if cache list is full
    b = list_entry(list_pop_back(&cache_list), struct buffer, elem);
    lock_acquire(&b->change_data);
  } else { // create new buffer entry
    for (int i = 0; i < 64; i++) {
      b = &cache[i];
      lock_acquire(&b->change_data);
      if (b->valid == 0) {
        break;
      }
      lock_release(&b->change_data);
    }
  }
  if (b->dirty == 1) { // if evicted block has dirty bit, write back to disk
    write_back(b);
  }
  b->sect_num = sect_num;
  b->dirty = 1;
  b->valid = 1;
  //block_write(fs_device, sect_num, &b->data); // actually read from disk
  memcpy(&b->data, buf, BLOCK_SECTOR_SIZE);
  lock_release(&b->change_data);
  list_push_front(&cache_list, &b->elem);
  lock_release(&lru_permission);
}

void buffer_evict(block_sector_t sect_num) {
  lock_acquire(&lru_permission);
  bool again = true;
  while (again) {
    again = false;
    for (int i = 0; i < 64; i++) {
      if (cache[i].sect_num == sect_num && cache[i].valid == 1) { // cache hit
        lock_release(&lru_permission);

        lock_acquire(&cache[i].change_data);
        if (cache[i].sect_num == sect_num) {
          cache[i].valid = 0;
          lock_release(&cache[i].change_data);
          /*move to front*/
          lock_acquire(&lru_permission);
          list_remove(&cache[i].elem);
          lock_release(&lru_permission);
          if (cache[i].dirty == 1)
            write_back(&cache[i]);
          cache[i].dirty = 0;
          return;
        }
        lock_release(&cache[i].change_data);
        lock_acquire(&lru_permission);
        again = true; /*try again */
      }
    }
  }
}

void buffer_flush() { // called during filesys_done
  for (int i = 0; i < 64; i++) {
    cache[i].valid = 0;
    if (cache[i].dirty == 1) {
      write_back(&cache[i]); // write the entire
    }
  }
}
