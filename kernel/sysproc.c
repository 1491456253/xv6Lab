#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  backtrace();
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  struct proc* p;

  p = myproc();

/*使用 argint() 和 argaddr() 函数从用户空间获取 interval 和 handler 参数的值。
如果这两个函数中的任何一个返回值小于0，则函数返回-1。*/
  if (argint(0, &interval) < 0 || argaddr(1, &handler) < 0) {
    return -1;
  }
  p->siginterval = interval;
  p->sigfunc = handler;
  /*如果参数获取成功，sys_sigalarm 函数将当前进程的 
  siginterval 字段设置为 interval 的值，并将 sigfunc 字段设置为 handler 的值。
  这样做是为了在进程的 proc 结构体中保存定时器的配置信息。*/
  return 0;
}

/*用于在信号处理程序执行完毕后恢复进程的执行状态。*/
uint64
sys_sigreturn(void)
{
  /*首先，函数通过调用 myproc() 函数获取当前进程的 proc 结构体。
  然后，它使用 memmove() 函数将 sigframe 中保存的 trapframe 
  结构体复制回进程的 trapframe 字段。这样做是为了恢复进程在接收信号之前的执行状态。
  接下来，函数将进程的 isentry 字段设置为0，表示进程当前不在信号处理程序中。*/
  struct proc* p = myproc();
  memmove(p->trapframe, p->sigframe, sizeof(struct trapframe));
  p->isentry = 0;
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  unsigned int procmask = 0 ;
  uint64 mask;  
  int len;  
 
  pagetable_t pagetable = 0;  
  
  pte_t *pte;
  uint64 base;
  struct proc *p = myproc();
 
  if(argaddr(0, &base) < 0 || argint(1, &len) < 0 || argaddr(2, &mask) < 0)//使用argaddr()和argint()解析参数
    return -1;
  if (len > sizeof(int)*8) 
    len = sizeof(int)*8;
 
  for(int i=0; i<len; i++) {
    pagetable = p->pagetable;
      
    if(base >= MAXVA)
      panic("pgaccess");
 
    for(int level = 2; level > 0; level--) {
      pte = &pagetable[PX(level, base)];
      if(*pte & PTE_V) {
        pagetable = (pagetable_t)PTE2PA(*pte);
      } else {
        return -1;
      }      
    }
    pte = &pagetable[PX(0, base)];
    if(pte == 0)
      return -1;
    if((*pte & PTE_V) == 0)
      return -1;
    if((*pte & PTE_U) == 0)
      return -1;  
    if(*pte & PTE_A) {  
      procmask = procmask | (1L << i);
      *pte = *pte & (~PTE_A);
    }
    base += PGSIZE;
  }
 
  pagetable = p->pagetable;
  return copyout(pagetable, mask, (char *) &procmask, sizeof(unsigned int));

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
