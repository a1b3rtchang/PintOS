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

bool allocate_direct(struct inode_disk*, off_t);
bool allocate_indirect(block_sector_t*, off_t);
bool allocate_db_indirect(block_sector_t*, off_t);

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
  char buffer[512];
  buffer_read(fs_device, inode->sector, (void*)buffer);
  disk = (struct inode_disk*)buffer;
  if (index >= disk->length || index >= 8388608)
    return -1;
  if (0 <= index && index < 122)
    return disk->direct_ptr[index];
  if (122 <= index && index < 122 + 128) {
    struct indirect* idp;
    char buffer_idp[512];
    buffer_read(fs_device, disk->indirect_ptr, (void*)buffer_idp); //fs_device???
    idp = (struct indirect*)buffer_idp;
    return idp->pointers[index - 122];
  }
  if (122 + 128 <= index && index < (int)(8388608 / 512)) {
    struct indirect *idp1, *idp2;
    char buffer_idp1[512];
    char buffer_idp2[512];
    buffer_read(fs_device, disk->db_indirect_ptr, (void*)buffer_idp1); //fs_device???
    idp1 = (struct indirect*)buffer_idp1;
    buffer_read(fs_device, idp1->pointers[(int)((index - 122 - 128) / 128)],
                (void*)buffer_idp2); //fs_device???
    idp2 = (struct indirect*)buffer_idp2;
    return idp2->pointers[(index - 122 - 128) % 128];
  }
  return -1;

  /* OLD IMPLEMENTATION
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
    OLD IMPLEMENTATION */
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  buffer_init();
  lock_init(&inode_open_lock);
  lock_init(&open_inodes_lock);
}

bool allocate_direct(struct inode_disk* disk, off_t length) {
  int i = 0;
  bool success;
  while (i < length && i < 122) {
    success = free_map_allocate(1, &disk->direct_ptr[i]);
    if (!success) {
      // uno reverse
      return success;
    }
    i++;
  }
  return success;
}

bool allocate_indirect(block_sector_t* ptr, off_t length) {
  free_map_allocate(1, ptr);
  char buffer[512];
  struct indirect* indirect_ptr = (struct indirect*)buffer;
  int i = 122;
  bool success;
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

bool allocate_db_indirect(block_sector_t* ptr, off_t length) {
  free_map_allocate(1, ptr);
  char buffer[512];
  // buffer_read(fs_device, ptr, (void*)&buffer);
  struct indirect* db_indirect_ptr = (struct indirect*)buffer;
  int i = 122 + 128;
  bool success;
  while (i < length) {
    success = allocate_indirect(&db_indirect_ptr->pointers[(int)((i - 122 - 128) / 128)], length);
    if (!success) {
      // uno reverse
      return success;
    }
    i += 128;
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

  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  char buffer[512];
  disk_inode = (struct inode_disk*)buffer;
  if (disk_inode != NULL) {
    off_t num_sectors = length / BLOCK_SECTOR_SIZE;
    success = allocate_direct(disk_inode, num_sectors);
    if (success) {
      success = allocate_indirect(&disk_inode->indirect_ptr, num_sectors);
    }
    if (success) {
      success = allocate_db_indirect(&disk_inode->db_indirect_ptr, num_sectors);
    }
    buffer_write(fs_device, sector, (void*)buffer);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  lock_acquire(&inode_open_lock);
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&inode_open_lock);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL) {
    lock_release(&inode_open_lock);
    return NULL;
  }

  /* Initialize. */
  lock_acquire(&open_inodes_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  char dummy[512];
  buffer_read(fs_device, inode->sector, (void*)dummy);
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
      free_map_release(inode->sector, 1);
      //free_map_release(inode->data.start, bytes_to_sectors(inode->data.length));
    }

    free(inode);
  }
  lock_release(&inode->lock);
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
  // uint8_t* bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  if (inode_length(inode) < offset + size) {
    //Choji Expansion Jutsu
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
  char buffer[512];
  buffer_read(fs_device, inode->sector, (void*)buffer);
  struct inode_disk* id = (struct inode_disk*)buffer;
  return id->length;
}