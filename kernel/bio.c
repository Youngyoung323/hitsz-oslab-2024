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

#define NBUCKETS 13

int CalcHash(uint blockno);

struct {
  struct spinlock lock[NBUCKETS];
  struct spinlock eviction_lock;  // 全局驱逐锁，防止一个数据块写入两个buf
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
  struct buf hashbucket[NBUCKETS];  // 每个hash桶都有一个lock和队列，一个hashbucket可以看做一个头节点
} bcache;

void
binit(void)
{
  struct buf *b;

  // 初始化所有的hashbucket
  for(int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    // Create linked list of buffers
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }
  
  // 将缓存块分配给不同hash桶，通过哈希函数进行映射
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // 要保证分配的数量尽量平均
    int hashIndex = (uint64)(b - bcache.buf) % NBUCKETS;

    b->next = bcache.hashbucket[hashIndex].next;
    b->prev = &bcache.hashbucket[hashIndex];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[hashIndex].next->prev = b;
    bcache.hashbucket[hashIndex].next = b;
  }

  initlock(&bcache.eviction_lock, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  // 需要根据blockno找到对应的哈希桶
  // 和上面一样简单求余
  //int hashIndex = blockno % NBUCKETS;
  int hashIndex = CalcHash(blockno);
  acquire(&bcache.lock[hashIndex]);

  // Is the block already cached?
  for(b = bcache.hashbucket[hashIndex].next; b != &bcache.hashbucket[hashIndex]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hashIndex]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 为了避免死锁，我们不可以在自己持有lock的时候去申请另一个bucket的lock
  // 但如果我们破坏保持请求条件，先释放自己持有的lock，极端情况下会出现一个数据块被写入两块缓存之中
  // 为了避免这种情况我们需要设置一个全局的驱逐锁，并在获得该锁后再次检查数据是否被写入缓存当中
  release(&bcache.lock[hashIndex]);
  acquire(&bcache.eviction_lock);   // 获取驱逐锁

  // 再次检查是否前面有进程进行了缓存块的替换/窃取
  acquire(&bcache.lock[hashIndex]);
  for(b = bcache.hashbucket[hashIndex].next; b != &bcache.hashbucket[hashIndex]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      // b->refcnt++ 保证其是原子操作
      b->refcnt++;
      release(&bcache.lock[hashIndex]);
      release(&bcache.eviction_lock);  // 多释放一个全局驱逐锁
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // 查询自己内部是否还有空闲缓存块
  for(b = bcache.hashbucket[hashIndex].prev; b != &bcache.hashbucket[hashIndex]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[hashIndex]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[hashIndex]);

  // No free cache to recycle itself
  // 寻找别的哈希桶是否还有空闲的内存块可以重新分配给自己
  for(int i = 0; i < NBUCKETS; i++) {
    if(i == hashIndex) continue;  // 跳过自己
    acquire(&bcache.lock[i]);
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev) {
      if(b->refcnt == 0) {
        acquire(&bcache.lock[hashIndex]);
        // 把找到的b结点从原来的链表中删除
        b->next->prev = b->prev;
        b->prev->next = b->next;
        // 把找到的b插入到哈希桶中
        b->next = bcache.hashbucket[hashIndex].next;
        b->prev = &bcache.hashbucket[hashIndex];
        bcache.hashbucket[hashIndex].next->prev = b;
        bcache.hashbucket[hashIndex].next = b; 
        // 修改buf的数值字段
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.lock[i]);
        release(&bcache.lock[hashIndex]);
        release(&bcache.eviction_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[i]); // 该桶找不到则释放lock
  }
  release(&bcache.eviction_lock); // 实在找不到就释放驱逐锁

  // All hashbucket no free buffer
  panic("bget: no buffers");
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
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hashIndex = CalcHash(b->blockno);
  acquire(&bcache.lock[hashIndex]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[hashIndex].next;
    b->prev = &bcache.hashbucket[hashIndex];
    bcache.hashbucket[hashIndex].next->prev = b;
    bcache.hashbucket[hashIndex].next = b;
  }
  
  release(&bcache.lock[hashIndex]);
}

void
bpin(struct buf *b) {
  int hashIndex = CalcHash(b->blockno);
  acquire(&bcache.lock[hashIndex]);
  b->refcnt++;
  release(&bcache.lock[hashIndex]);
}

void
bunpin(struct buf *b) {
  int hashIndex = CalcHash(b->blockno);
  acquire(&bcache.lock[hashIndex]);
  b->refcnt--;
  release(&bcache.lock[hashIndex]);
}

int CalcHash(uint blockno) {
  return blockno % NBUCKETS;
}
