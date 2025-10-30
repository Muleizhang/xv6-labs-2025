#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  memmove(p->trapframe, p->trapframe_saved, sizeof(struct trapframe));
  kfree(p->trapframe_saved);
  p->using_handler = 0;
  return p->trapframe->a0;
}

uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  argint(0,&interval);
  argaddr(1, &handler);

  struct proc *p = myproc();
  p->interval = interval;
  p->handler = handler;

  return 0;
}
