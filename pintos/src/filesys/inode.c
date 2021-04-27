#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static struct lock inode_open_lock;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  //block_sector_t start; /* First data sector. */
  int is_directory;
  int idklmfao;
  off_t length;                   /* File size in bytes. */
  unsigned magic;                 /* Magic number. */
  block_sector_t indirect_ptr;    /* Indirect Pointer */
  block_sector_t db_indirect_ptr; /* Doubly Indirect Pointer */
  block_sector_t direct_ptr[122]; /* Not used. */
};

struct indirect {
  block_sector_t pointers[128];
};

bool allocate_direct(struct inode_disk*, off_t, off_t);
bool allocate_indirect(block_sector_t*, off_t, off_t);
bool allocate_db_indirect(block_sector_t*, off_t, off_t);
bool expand_inode_disk(struct inode_disk*, off_t);
void shrink_inode_disk(struct inode_disk*, off_t);
bool resize_inode(struct inode*, off_t);
bool sanity_check(void);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
  struct lock lock;      /* Protects metadata from race conditions. */
  //struct inode_disk data; /* Inode content. */
};
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  int index = (int)(pos / 512);
  struct inode_disk* disk;
  char* buffer = malloc(sizeof(char) * 512);
  struct indirect* idp;
  char* buffer_idp = malloc(sizeof(char) * 512);
  struct indirect *idp1, *idp2;
  char* buffer_idp1 = malloc(sizeof(char) * 512);
  char* buffer_idp2 = malloc(sizeof(char) * 512);
  block_sector_t rv;
  buffer_read(fs_device, inode->sector, (void*)buffer);
  disk = (struct inode_disk*)buffer;
  if (index >= disk->length || index >= 8388608) {
    rv = -1;
  }
  if (0 <= index && index < 122) {
    rv = disk->direct_ptr[index];
  }
  if (122 <= index && index < 122 + 128) {

    buffer_read(fs_device, disk->indirect_ptr, (void*)buffer_idp); //fs_device???
    idp = (struct indirect*)buffer_idp;
    rv = idp->pointers[index - 122];
  }
  if (122 + 128 <= index && index < (int)(8388608 / 512)) {
    buffer_read(fs_device, disk->db_indirect_ptr, (void*)buffer_idp1); //fs_device???
    idp1 = (struct indirect*)buffer_idp1;
    buffer_read(fs_device, idp1->pointers[(int)((index - 122 - 128) / 128)],
                (void*)buffer_idp2); //fs_device???
    idp2 = (struct indirect*)buffer_idp2;
    rv = idp2->pointers[(index - 122 - 128) % 128];
  }
  free(buffer);
  free(buffer_idp);
  free(buffer_idp1);
  free(buffer_idp2);
  return rv;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;
static struct lock inode_close_lock;

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  buffer_init();
  lock_init(&inode_open_lock);
  lock_init(&open_inodes_lock);
  lock_init(&inode_close_lock);
}

bool sanity_check() {
  block_sector_t test;
  free_map_allocate(1, &test);
  inode_create(test, 10);
  char* str = "KENNY CHI";
  char buf[10];
  struct inode* inode = inode_open(test);
  ASSERT(inode != NULL);
  inode_write_at(inode, str, 10, 0);
  inode_read_at(inode, buf, 10, 0);
  ASSERT(strcmp(buf, "KENNY CHI") == 0);
  return true;
}

bool expand_inode_disk(struct inode_disk* disk, off_t length) {
  int original_length = disk->length;
  int index = (int)(disk->length / 512) + 1;
  int last = (int)(length / 512);
  char* buffer_idp1 = malloc(sizeof(char) * 512);
  char* buffer_idp2 = malloc(sizeof(char) * 512);
  struct indirect *idp1, *idp2;
  bool success = true;
  while (index <= last) {
    if (index >= 8388608) {
      free(buffer_idp1);
      free(buffer_idp2);
      return false;
    }
    if (0 <= index && index < 122) {
      success = free_map_allocate(1, &disk->direct_ptr[index]);
      if (!success) {
        shrink_inode_disk(disk, original_length);
        free(buffer_idp1);
        free(buffer_idp2);
        return false;
      }
    }
    if (122 <= index && index < 122 + 128) {
      if (index == 122) { //first time here so must allocate indirect struct
        success = free_map_allocate(1, &disk->indirect_ptr);
      }
      if (!success) {
        shrink_inode_disk(disk, original_length);
        free(buffer_idp1);
        free(buffer_idp2);
        return false;
      }
      buffer_read(fs_device, disk->indirect_ptr, (void*)buffer_idp1); //fs_device???
      idp1 = (struct indirect*)buffer_idp1;
      success = free_map_allocate(1, &idp1->pointers[index - 122]);
      if (!success) {
        free_map_release(disk->indirect_ptr, 1); //dealloc
        shrink_inode_disk(disk, original_length);
        free(buffer_idp1);
        free(buffer_idp2);
        return false;
      }
      buffer_write(fs_device, disk->indirect_ptr, (void*)buffer_idp1);
    }
    if (122 + 128 <= index && index < (int)(8388608 / 512)) {
      if (index == 122 + 128) {
        success = free_map_allocate(1, &disk->db_indirect_ptr);
      }
      if (!success) {
        shrink_inode_disk(disk, original_length);
        free(buffer_idp1);
        free(buffer_idp2);
        return false;
      }
      buffer_read(fs_device, disk->db_indirect_ptr, (void*)buffer_idp1); //fs_device???
      idp1 = (struct indirect*)buffer_idp1;
      if ((index - 122 - 128) % 128 == 0) {
        success = free_map_allocate(1, &idp1->pointers[(int)((index - 122 - 128) / 128)]);
      }
      if (!success) {
        free_map_release(disk->db_indirect_ptr, 1);
        shrink_inode_disk(disk, original_length);
        free(buffer_idp1);
        free(buffer_idp2);
        return false;
      }
      buffer_read(fs_device, idp1->pointers[(int)((index - 122 - 128) / 128)],
                  (void*)buffer_idp2); //fs_device???
      idp2 = (struct indirect*)buffer_idp2;
      success = free_map_allocate(1, &idp2->pointers[(index - 122 - 128) % 128]);
      if (!success) {
        free_map_release(disk->db_indirect_ptr, 1);
        free_map_release(idp1->pointers[(int)((index - 122 - 128) / 128)], 1);
        shrink_inode_disk(disk, original_length);
        free(buffer_idp1);
        free(buffer_idp2);
        return false;
      }
      buffer_write(fs_device, idp1->pointers[(int)((index - 122 - 128) / 128)], (void*)buffer_idp2);
      buffer_write(fs_device, disk->db_indirect_ptr, (void*)buffer_idp1);
    }
    index++;
    disk->length += 512;
  }
  disk->length = length;
  free(buffer_idp1);
  free(buffer_idp2);
  return true;
}

void shrink_inode_disk(struct inode_disk* disk, off_t length) {
  int last = (int)(disk->length / 512) + 1;
  int index = (int)(length / 512);
  char* buffer_idp1 = malloc(sizeof(char) * 512);
  char* buffer_idp2 = malloc(sizeof(char) * 512);
  struct indirect *idp1, *idp2;
  while (index >= last) {
    if (0 <= index && index < 122) {
      free_map_release(disk->direct_ptr[index], 1);
    }
    if (122 <= index && index < 122 + 128) {
      buffer_read(fs_device, disk->indirect_ptr, (void*)buffer_idp1); //fs_device???
      idp1 = (struct indirect*)buffer_idp1;
      free_map_release(idp1->pointers[index - 122], 1);
      if (index == 122) { //first time here so must allocate indirect struct
        free_map_release(disk->indirect_ptr, 1);
      } else {
        buffer_write(fs_device, disk->indirect_ptr, (void*)buffer_idp1);
      }
    }
    if (122 + 128 <= index && index < (int)(8388608 / 512)) {
      buffer_read(fs_device, disk->db_indirect_ptr, (void*)buffer_idp1); //fs_device???
      idp1 = (struct indirect*)buffer_idp1;
      buffer_read(fs_device, idp1->pointers[(int)((index - 122 - 128) / 128)],
                  (void*)buffer_idp2); //fs_device???
      idp2 = (struct indirect*)buffer_idp2;
      free_map_release(idp2->pointers[(index - 122 - 128) % 128], 1);
      if ((index - 122 - 128) % 128 == 0) {
        free_map_release(idp1->pointers[(int)((index - 122 - 128) / 128)], 1);
      } else {
        buffer_write(fs_device, idp1->pointers[(int)((index - 122 - 128) / 128)],
                     (void*)buffer_idp2);
      }
      if (index == 122 + 128) {
        free_map_release(disk->db_indirect_ptr, 1);
      } else {
        buffer_write(fs_device, disk->db_indirect_ptr, (void*)buffer_idp1);
      }
    }
    index--;
    disk->length -= 512;
  }
  free(buffer_idp1);
  free(buffer_idp2);
  disk->length = length;
}

bool resize_inode(struct inode* inode, off_t new_length) {
  char* buffer = malloc(sizeof(char) * 512);
  bool success = true;
  buffer_read(fs_device, inode->sector, buffer);
  struct inode_disk* disk = (struct inode_disk*)buffer;
  if (new_length > disk->length) {
    success = expand_inode_disk(disk, new_length);
    if (success) {
      buffer_write(fs_device, inode->sector, buffer);
    }
  } else if (new_length < disk->length) {
    shrink_inode_disk(disk, new_length);
    buffer_write(fs_device, inode->sector, buffer);
  }
  free(buffer);
  return success;
}

bool allocate_direct(struct inode_disk* disk, off_t start, off_t length) {
  int i = start;
  bool success = true;
  while (i < length && i < 122) {
    success = free_map_allocate(1, &disk->direct_ptr[i]);
    if (!success) {
      // uno reverse
      return success;
    }
    i++;
  }
  disk->length = length;
  return success;
}

bool allocate_indirect(block_sector_t* ptr, off_t start, off_t length) {
  free_map_allocate(1, ptr);
  char buffer[512];
  struct indirect* indirect_ptr = (struct indirect*)buffer;
  int i = 122 > start ? 122 : start;
  bool success = true;
  while (i < length && i < 122 + 128) {
    success = free_map_allocate(1, &indirect_ptr->pointers[i - 122]);
    if (!success) {
      // uno reverse
      return success;
    }
    i++;
  }
  buffer_write(fs_device, *ptr, buffer);
  return success;
}

bool allocate_db_indirect(block_sector_t* ptr, off_t start, off_t length) {
  free_map_allocate(1, ptr);
  char buffer[512];
  // buffer_read(fs_device, ptr, (void*)&buffer);
  struct indirect* db_indirect_ptr = (struct indirect*)buffer;
  int i = (122 + 128) > start ? (122 + 128) : start;
  i -= 122 + 128;
  bool success = true;
  while (i < length - 122 - 128) {
    success = allocate_indirect(&db_indirect_ptr->pointers[(int)(i / 128)], i % 128,
                                length - i < 128 - (i % 128) ? length - i : 128 - (i % 128));
    if (!success) {
      // uno reverse
      return success;
    }
    i += 128 - (i % 128);
  }
  buffer_write(fs_device, *ptr, buffer);
  return success;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  bool success = false;

  ASSERT(length >= 0);
  struct inode_disk* disk_inode;
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  char* inode_buf = malloc(sizeof(char) * 512);
  struct inode* inode = (struct inode*)inode_buf;
  inode->open_cnt = 0;
  inode->removed = false;
  inode->deny_write_cnt = 0;
  success = free_map_allocate(1, &inode->sector);
  if (!success) {
    return false;
  }
  success = resize_inode(inode, length);
  if (!success) {
    free_map_release(inode->sector, 1);
  }
  buffer_write(fs_device, sector, (void*)inode_buf);
  /*  
  if (disk_inode != NULL) {
    off_t num_sectors = length / BLOCK_SECTOR_SIZE;
    success = allocate_direct(disk_inode, 0, num_sectors);
    if (success) {
      success = allocate_indirect(&disk_inode->indirect_ptr, 0, num_sectors);
    }
    if (success) {
      success = allocate_db_indirect(&disk_inode->db_indirect_ptr, 0, num_sectors);
    }
    disk_inode->length = length;
    buffer_write(fs_device, sector, (void*)buffer);
  }
*/
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  bool recurse = lock_held_by_current_thread(&inode_open_lock);
  if (!recurse)
    lock_acquire(&inode_open_lock);
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      if (!recurse)
        lock_release(&inode_open_lock);
      return inode;
    }
  }

  /* Allocate memory. */
  //inode = malloc(sizeof(char) * 512);
  char* buffer = malloc(sizeof(char) * 512);
  if (inode == NULL) {
    if (!recurse)
      lock_release(&inode_open_lock);
    return NULL;
  }

  buffer_read(fs_device, sector, (void*)buffer);
  inode = (struct inode*)buffer;
  int len = inode_length(inode);
  //free(inode);
  /* Initialize. */
  lock_acquire(&open_inodes_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  //buffer_read(fs_device, inode->sector, (void*)dummy);
  if (!recurse)
    lock_release(&inode_open_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  lock_acquire(&inode_open_lock);
  if (inode == NULL)
    return;
  lock_acquire(&inode->lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    lock_acquire(&open_inodes_lock);
    list_remove(&inode->elem);
    lock_release(&open_inodes_lock);
    /* Deallocate blocks if removed. */
    if (inode->removed) {
      // inode_create but uno reverse
      resize_inode(inode, 0);
      free_map_release(inode->sector, 1);
      //free_map_release(inode->data.start, bytes_to_sectors(inode->data.length));
    }
    buffer_evict(inode->sector);
    free(inode);
    return;
  }
  lock_release(&inode->lock);
  lock_release(&inode_open_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  // uint8_t* bounce = NULL;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    buffer_read(fs_device, sector_idx, buffer + bytes_read);

    // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    //   /* Read full sector directly into caller's buffer. */
    //   buffer_read(fs_device, sector_idx, buffer + bytes_read);
    // } else {
    //   /* Read sector into bounce buffer, then partially copy
    //          into caller's buffer. */
    //   if (bounce == NULL) {
    //     bounce = malloc(BLOCK_SECTOR_SIZE);
    //     if (bounce == NULL)
    //       break;
    //   }
    //   buffer_read(fs_device, sector_idx, bounce);
    //   memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    // }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  // free(bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  bool success = true;
  // uint8_t* bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  if (inode_length(inode) < offset + size) {
    success = resize_inode(inode, size + offset);
    if (!success)
      return 0;
    /*
    //Choji Expansion Jutsu
    off_t length = offset + size
    struct inode_disk* disk;
    char buffer[512];
    buffer_read(fs_device, inode->sector, (void*)buffer);
    disk = (struct inode_disk*)buffer;
    bool success = allocate_direct(disk, start, length);
    if (success) {
      success = allocate_indirect(&disk->indirect_ptr, start, length);
    }
    if (success) {
      success = allocate_db_indirect(&disk->db_indirect_ptr, start, length);
      } */
  }

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    buffer_write(fs_device, sector_idx, (void*)(buffer + bytes_written));
    // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    //   /* Write full sector directly to disk. */
    //   buffer_write(fs_device, sector_idx, (void*)(buffer + bytes_written));
    // } else {
    //   /* We need a bounce buffer. */
    //   if (bounce == NULL) {
    //     bounce = malloc(BLOCK_SECTOR_SIZE);
    //     if (bounce == NULL)
    //       break;
    //   }

    //   /* If the sector contains data before or after the chunk
    //          we're writing, then we need to read in the sector
    //          first.  Otherwise we start with a sector of all zeros. */
    //   if (sector_ofs > 0 || chunk_size < sector_left)
    //     buffer_read(fs_device, sector_idx, bounce);
    //   else
    //     memset(bounce, 0, BLOCK_SECTOR_SIZE);
    //   memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
    //   buffer_write(fs_device, sector_idx, bounce);
    // }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  // free(bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  lock_acquire(&inode->lock);
  inode->deny_write_cnt++;
  lock_release(&inode->lock);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  lock_acquire(&inode->lock);
  inode->deny_write_cnt--;
  lock_release(&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  char* buffer = malloc(sizeof(char) * 512);
  buffer_read(fs_device, inode->sector, (void*)buffer);
  struct inode_disk* id = (struct inode_disk*)buffer;
  return id->length;
}
