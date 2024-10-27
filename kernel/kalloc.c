// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void kfree_bycpu(void *pa,int cpu_id);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct kmem kmems[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {  // 每个CPU都是初始化一次lock
    initlock(&kmems[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);

  // 把空闲的内存空间均匀连续地分配给每个空闲链表
  uint64 total_size = (uint64)pa_end - (uint64)pa_start;  // 空闲内存大小
  uint64 total_page_numbers = total_size / PGSIZE;        // 空闲内存页有多少页
  uint64 single_cpu_pagenums = total_page_numbers / NCPU; // 每个CPU含多少页

  // 需要额外考虑total_page_numbers / NCPU有余数的情况
  uint64 extra_pagenums = total_page_numbers % NCPU;      // 余下的空闲页
  for (int i = 0; i < NCPU; i++) {
    uint64 real_pagenums = single_cpu_pagenums;
    if(extra_pagenums > 0) {
      real_pagenums++;
      extra_pagenums--;
    }

    // 将空闲页分配给每个空闲链表
    for (uint64 j = 0; j < real_pagenums; j++) {
      kfree_bycpu(p, i);
      p += PGSIZE;
    }
  }

}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  kfree_bycpu(pa, cpuid());
}

// 指定某个cpu执行free操作
void
kfree_bycpu(void *pa, int cpu_id) {
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu_id = cpuid();  // 获取当前cpu编号
  pop_off();

  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;
  if(r) {
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }
  else {  // 当前CPU持有的链表没有空闲页，需要steal其他CPU的空闲链表
    for (int i = 0; i < NCPU; i++) { 
      if(i == cpu_id) continue;  // 跳过检查自己

      acquire(&kmems[i].lock);
      r  = kmems[i].freelist;
      if(r) {
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        release(&kmems[cpu_id].lock);
        memset((char*)r, 5, PGSIZE); // fill with junk
        return (void*)r;
      }
      else {
        release(&kmems[i].lock);
      } 
    }
    release(&kmems[cpu_id].lock);
  }

  return 0;  // No free memory
}

