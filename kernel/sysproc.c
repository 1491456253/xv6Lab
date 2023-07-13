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


#ifdef LAB_PGTBL

// 在这里声明对kernel/vm.c/walk函数的引用声明
extern pte_t * walk(pagetable_t, uint64, int);


int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // 在内核态下声明一个BitMask，用来存放结果
  uint64 BitMask = 0;
  
  // 声明一些变量，用来接收用户态下传入的参数
  uint64 StartVA;
  int NumberOfPages;
  uint64 BitMaskVA;
  
  // 首先读取要访问的页面数量
  if(argint(1, &NumberOfPages) < 0)
    return -1;
  
  // 如果页面数量超过了一次可以读取的最大范围
  // 系统调用直接返回
  if(NumberOfPages > MAXSCAN)
    return -1;                    
  
  // 读取页面开始地址和指向用户态存放结果的BitMask的指针
  if(argaddr(0, &StartVA) < 0)
    return -1;
  if(argaddr(2, &BitMaskVA) < 0)
    return -1;

  int i;
  pte_t* pte;
	
  // 从起始地址开始，逐页判断PTE_A是否被置位
  // 如果被置位，则设置对应BitMask的位，并将PTE_A清空
  for(i = 0 ; i < NumberOfPages ; StartVA += PGSIZE, ++i){
    if((pte = walk(myproc()->pagetable, StartVA, 0)) == 0)
      panic("pgaccess : walk failed");
    if(*pte & PTE_A){
      BitMask |= 1 << i;	// 设置BitMask对应位
      *pte &= ~PTE_A;		// 将PTE_A清空
    }
  }
  
  // 最后使用copyout将内核态下的BitMask拷贝到用户态
  copyout(myproc()->pagetable, BitMaskVA, (char*)&BitMask, sizeof(BitMask));
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
