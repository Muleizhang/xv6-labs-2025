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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// lab cow: 存储page被引用的次数
struct {
  struct spinlock lock;
  int ref_counts[PHYSTOP / PGSIZE];
} page_refs;

void
increase_ref(uint64 pa)
{
  acquire(&page_refs.lock);
  if (page_refs.ref_counts[pa/PGSIZE] < 1) {
    panic("incref: ref count < 1");
  }
  page_refs.ref_counts[pa/PGSIZE] ++;
  release(&page_refs.lock);
}

int
get_ref(uint64 pa)
{
  acquire(&page_refs.lock);
  int count = page_refs.ref_counts[pa/PGSIZE];
  release(&page_refs.lock);
  return count;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_refs.lock, "page_refs"); // init page_refs lock
  freerange(end, (void*)PHYSTOP);
}

// 唯一一次被调用是在kinit中清理出内存页
// 注意不要用于其他地方，freerange不会检查内存页是否被引用
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfreerange(p);
}

// kfreerange唯一用处是在freerange的时候清理出内存页
// 区别于kfree，kfreerange不会检查内存页是否被引用
void
kfreerange(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  
  acquire(&page_refs.lock);
  // 检查是否有被引用
  if (page_refs.ref_counts[(uint64)pa / PGSIZE] <= 0) {
    panic("kfree: count <= 0"); 
  }
  // 自身销毁，引用数减一
  page_refs.ref_counts[(uint64)pa / PGSIZE] --;
  int ref_counts = page_refs.ref_counts[(uint64)pa / PGSIZE];
  release(&page_refs.lock);
  // 只有在无引用状态下才进行free
  if (ref_counts > 0) {
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    acquire(&page_refs.lock);
    if (page_refs.ref_counts[(uint64)r / PGSIZE] != 0) {
      panic("ref count != 0"); 
    }
    page_refs.ref_counts[(uint64)r / PGSIZE] = 1;
    release(&page_refs.lock);

    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}
