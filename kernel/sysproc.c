#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_yield(void) {
  struct proc *p = myproc();
  
  acquire(&p->lock);  // 打印时对当前进程加锁
  // 打印当前进程上下文的地址范围,尤其注意指针偏移的运算
  // 应当加上char*使得指针地址可以正确偏移,这里的加1不是传统意义上的加1，对于char*来说是移动一个字节
  // 不加上char*会使得指针地址偏移了sizeof(p->context)*sizeof(context_type)个字节
  printf("Save the context of the process to the memory region from address %p to %p\n", &p->context, (char *)&p->context + sizeof(p->context));
  // 打印当前进程的进程号以及陷入pc
  printf("Current running process pid is %d and user pc is %p\n", p->pid, p->trapframe->epc);
  release(&p->lock);

  // 模拟寻找下一个runnable的进程,但是不将其写入cpu
  int found = 0;
  struct proc *np;
  for (np = proc; np < &proc[NPROC]; np++) {  // NPROC为进程最大数量，遍历所有进程
    acquire(&np->lock);              // 对该进程进行操作时上锁
    if(np->state == RUNNABLE) {
      printf("Next runnable process pid is %d and user pc is %p\n", np->pid, np->trapframe->epc);
      found = 1;
      release(&np->lock);
      break;                         // 只需要模拟一次调度
    }
    release(&np->lock);              // 有可能找不到可以调度的进程
  }

  if (!found) {
    printf("No RUNNABLE process!");
  }
  
  yield();
  return 0;
}

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int n;
  if (argaddr(0, &p) < 0) return -1;
  if (argint(1, &n) < 0) return -1;  // 传入的是1才能实现非阻塞调用
  return wait(p, n);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}
