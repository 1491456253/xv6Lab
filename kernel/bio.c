// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUFMAP_BUCKET 13
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

struct {
  struct buf buf[NBUF];
  struct spinlock eviction_locks[NBUFMAP_BUCKET];

  // Hash map: dev and blockno to buf
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];
} bcache;

void
binit(void)
{
  /*遍历了所有的桶，并使用initlock函数初始化了每个桶的两个锁：
  bcache.eviction_locks[i]和bcache.bufmap_locks[i]。
  此外，代码还将每个桶的next指针初始化为0。*/
  for(int i=0;i<NBUFMAP_BUCKET;i++) {
    initlock(&bcache.eviction_locks[i], "bcache_eviction");
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }

 /*代码使用initsleeplock函数初始化了缓冲区的锁，
 并将缓冲区的lastuse和refcnt字段初始化为0。
 然后，代码将所有缓冲区都放入了第0个桶（即bufmap[0]）中。*/
  for(int i=0;i<NBUF;i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    // put all the buffers into bufmap[0]
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  /*在块缓存中查找一个包含指定设备和块号的缓冲区。如果找到了这样一个缓冲区，
  则函数会增加该缓冲区的引用计数，并返回指向该缓冲区的指针。
  如果没有找到这样一个缓冲区，则函数会选择一个未使用的缓冲区，
  将其初始化为包含指定设备和块号的数据，并返回指向该缓冲区的指针。*/
  struct buf *b;

  uint key = BUFMAP_HASH(dev, blockno);

  // printf("dev: %d, blockno: %d, locked: %d\n", dev, blockno, bcache.bufmap_locks[key].locked);
  
  acquire(&bcache.bufmap_locks[key]);

  // Is the block already cached?
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.

  // to get a suitable block to reuse, we need to search for one in all the buckets,
  // which means acquiring their bucket locks.
  // but it's not safe to try to acquire every single bucket lock while holding one.
  // it can easily lead to circular wait, which produces deadlock.

  release(&bcache.bufmap_locks[key]);
  /*这段代码首先计算出块号在哈希表中的桶索引，并获取该桶的锁。
  然后，代码遍历该桶中的所有缓冲区，查找是否有一个缓冲区已经包含了指定设备和块号的数据。
  如果找到了这样一个缓冲区，则代码会增加该缓冲区的引用计数，并释放桶锁。
  然后，代码获取该缓冲区的锁，并返回指向该缓冲区的指针。*/
  acquire(&bcache.eviction_locks[key]);

  /*如果没有找到包含指定设备和块号的缓冲区，则代码会释放桶锁，
  并获取驱逐锁。然后，代码再次遍历该桶中的所有缓冲区，
  以确保在释放桶锁后没有其他线程创建了新的缓冲区。
  如果仍然没有找到包含指定设备和块号的缓冲区，则代码会执行驱逐操作，
  选择一个未使用的缓冲区并将其初始化为包含指定设备和块号的数据。*/
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]); // must do, for `refcnt++`
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Still not cached.
  // we are now only holding eviction lock, none of the bucket locks are held by us.
  // so it's now safe to acquire any bucket's lock without risking circular wait and deadlock.

  // find the one least-recently-used buf among all buckets.
  // finish with it's corresponding bucket's lock held.
  struct buf *before_least = 0; 
  uint holding_bucket = -1;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    // before acquiring, we are either holding nothing, or only locks of
    // buckets that are *on the left side* of the current bucket
    // so no circular wait can ever happen here. (safe from deadlock)
    acquire(&bcache.bufmap_locks[i]);
    int newfound = 0; // new least-recently-used buf found in this bucket
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound) {
      release(&bcache.bufmap_locks[i]);
    } else {
      if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
      // keep holding this bucket's lock....
    }
  }
  if(!before_least) {
    panic("bget: no buffers");
  }
  b = before_least->next;
  
  if(holding_bucket != key) {
    // remove the buf from it's original bucket
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);
    // rehash and add it to the correct bucket
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_locks[key]);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
	virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastuse = ticks;
  }
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  /*这段代码首先计算出块号在哈希表中的桶索引，并获取该桶的锁。
  然后，代码增加缓冲区的引用计数。最后，代码释放桶锁并返回。*/
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}
