#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif  

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
#ifdef LAB_PGTBL
      if(PTE_LEAF(*pte)) {
        return pte;
      }
#endif
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

pte_t *
swalk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("swalk");

  int level = 2;
  pte_t *pte = &pagetable[PX(level, va)];
  if(*pte & PTE_V) {
    pagetable = (pagetable_t)PTE2PA(*pte);
  } else {
    if(!alloc || (pagetable = (pde_t*)kalloc()) == 0) // Allocates 4KB (PGSIZE)
        return 0;
    memset(pagetable, 0, PGSIZE);
    *pte = PA2PTE(pagetable) | PTE_V;
  }
  // note this is level 1
  return &pagetable[PX(1, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}


#if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)

#define PTE_G 1L<<2
#define PTE_A 1L<<6
#define PTE_D 1L<<7
#define PTXSHIFT(level) (12 + (level) * 9)

void
vmprint_recursive(pagetable_t pagetable, int level, uint64 va_start) {
  uint64 shift = PTXSHIFT(level); // L2: 30, L1: 21, L0: 12
  for (int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    uint64 new_start = va_start + ((uint64)i << shift);    
    if (pte & PTE_V){
      for (int k = 0; k < 3 - level; k++) {
        printf(" ..");
      }
      /*
      for (int i = 63; i >= 0; i--) {
        (new_start & (1 << i))? printf("1") : printf("0");
      }
      */

      printf("%p: pte %p pa %p", (uint64*)(new_start), (uint64*)pte, (uint64*)PTE2PA(pte));
      if (PTE_LEAF(pte)) {
        printf("   ");
        printf((pte & PTE_V) ? "V" : "-");
        printf((pte & PTE_R) ? "R" : "-");
        printf((pte & PTE_X) ? "X" : "-");
        printf((pte & PTE_W) ? "W" : "-");
        printf((pte & PTE_U) ? "U" : "-");
        printf((pte & PTE_G) ? "G" : "-");
        printf((pte & PTE_A) ? "A" : "-");
        printf((pte & PTE_D) ? "D" : "-");
        printf("\n");
      } else {
        for (int i = 0;i < level;i++) {
          printf("   ");
        }
        printf("   ");
        printf((pte & PTE_V) ? "V" : "-");
        printf((pte & PTE_R) ? "R" : "-");
        printf((pte & PTE_X) ? "X" : "-");
        printf((pte & PTE_W) ? "W" : "-");
        printf((pte & PTE_U) ? "U" : "-");
        printf((pte & PTE_G) ? "G" : "-");
        printf((pte & PTE_A) ? "A" : "-");
        printf((pte & PTE_D) ? "D" : "-");
        printf("\n");        
        vmprint_recursive((uint64*)PTE2PA(pte), level - 1, new_start);
      }
    }
  }
}

void
vmprint(pagetable_t pagetable) {
  printf("page table %p\n",pagetable);
  vmprint_recursive(pagetable, 2, 0);
}
#endif



// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;
  uint64 pgsize;

  if (pa >= SUPER_PAGE_START) {
    pgsize = SUPERPGSIZE;
  } else {
    pgsize = PGSIZE;
  }

  if((va % pgsize) != 0)
    panic("mappages: va not aligned");

  if((size % pgsize) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - pgsize;
  for(;;){
    if (pgsize == PGSIZE) {
      // 4k
      if ((pte = walk(pagetable, a, 1)) == 0) 
        return -1;
    } else {
      // 2m
      if ((pte = swalk(pagetable, a, 1)) == 0) 
        return -1;
    }
    
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += pgsize;
    pa += pgsize;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; /* a is advanced inside */){
    if((pte = walk(pagetable, a, 0)) == 0){
      a += PGSIZE;
      continue;
    }

    // This check is critical. A PTE can be valid (V=1) but not a leaf
    // (if R,W,X are 0). Such PTEs are pointers to lower-level page tables
    // and should not be unmapped by this function directly. The loop will
    // naturally handle their children.
    if((*pte & PTE_V) == 0 || (*pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      a += PGSIZE;
      continue;
    }

    uint64 pa = PTE2PA(*pte);
    
    if (pa >= SUPER_PAGE_START) {
      uint64 super_va_start = SUPERPGROUNDDOWN(a);
      
      if(va <= super_va_start && (va + npages*PGSIZE) >= (super_va_start + SUPERPGSIZE)){
        if(do_free){
          sfree((void*)pa);
        }
        *pte = 0;
        a = super_va_start + SUPERPGSIZE;
      } else {
        uint flags = PTE_FLAGS(*pte);
        char* l0_table;
        if((l0_table = kalloc()) == 0)
          panic("uvmunmap: kalloc for new page table failed");
        memset(l0_table, 0, PGSIZE);

        for (int i = 0; i < 512; i++) {
          uint64 current_va = super_va_start + i * PGSIZE;
          if (current_va != a) {
            char* new_page_pa;
            if ((new_page_pa = kalloc()) == 0)
              panic("uvmunmap: kalloc for demotion page failed");
            
            memmove(new_page_pa, (void*)(pa + i * PGSIZE), PGSIZE);
            
            pte_t *l0_pte = &((pte_t*)l0_table)[PX(0, current_va)];
            *l0_pte = PA2PTE((uint64)new_page_pa) | flags;
          }
        }

        *pte = PA2PTE((uint64)l0_table) | PTE_V;
        
        if (do_free) {
          sfree((void*)pa);
        }
        a += PGSIZE;
      }
    } else {
      if(do_free){
        kfree((void*)pa);
      }
      *pte = 0;
      a += PGSIZE;
    }
  }
}


// Add this new helper function somewhere near the top of vm.c,
// for example, after the walk() function.
// It checks for a mapping without creating new page tables.
pte_t*
walk_noalloc(pagetable_t pagetable, uint64 va)
{
  if(va >= MAXVA)
    panic("walk_noalloc");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      // Check if it's a leaf PTE (a superpage). If so, stop.
      if((*pte & (PTE_R|PTE_W|PTE_X)) != 0)
        return pte;
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      return 0; // Path does not exist.
    }
  }
  return &pagetable[PX(0, va)];
}

uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  a = oldsz;

  while(a < newsz) {
    // Attempt to allocate a superpage if aligned and there is enough space.
    if((a % SUPERPGSIZE) == 0 && (a + SUPERPGSIZE) <= newsz) {
      int can_alloc_super = 0;

      // Manually walk to the L1 PTE to check if the slot is free.
      pte_t* l2_pte = &pagetable[PX(2, a)];
      if (!(*l2_pte & PTE_V)) {
        // The L1 page table doesn't even exist, so the slot is definitely free.
        can_alloc_super = 1;
      } else {
        // The L1 page table exists. Check the L1 PTE itself.
        pagetable_t l1_table = (pagetable_t)PTE2PA(*l2_pte);
        pte_t* l1_pte = &l1_table[PX(1, a)];
        if (!(*l1_pte & PTE_V)) {
          // The L1 PTE is not valid (V=0), so the slot is free.
          can_alloc_super = 1;
        }
      }

      if (can_alloc_super) {
        mem = salloc();
        if(mem != 0) { // <--- 检查 salloc() 是否 *成功*
          // salloc 成功，映射它
          memset(mem, 0, SUPERPGSIZE);
          if(mappages(pagetable, a, SUPERPGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
            sfree(mem);
            goto nomem;
          }
          a += SUPERPGSIZE;
          continue; // 继续尝试下一个超级页
        }
        // 如果 mem == 0 (salloc 失败), 退出 if 块,
        // "掉下去" 执行下面的 4KB 分配逻辑
      }
    }

    // Fallback: allocate a 4KB page.
    // This handles non-aligned allocations AND filling holes in a demoted superpage.
    
    // CRUCIAL FIX: Before allocating, check if this page is already mapped.
    pte_t* pte = walk_noalloc(pagetable, a);
    if(pte && (*pte & PTE_V)) {
      // This page is already mapped, likely part of a demoted superpage.
      // Do nothing and move to the next page.
      a += PGSIZE;
      continue;
    }

    // This page is unmapped. Allocate and map it now.
    mem = kalloc();
    if(mem == 0) goto nomem;
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      goto nomem;
    }
    a += PGSIZE;
  }
  return newsz;

nomem:
  uvmdealloc(pagetable, a, oldsz);
  return 0;
}


// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // backtrace();
      pagetable[i] = 0;
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc;

  for(i = 0; i < sz; i += szinc){
    if((pte = walk(old, i, 0)) == 0){
      printf("c");
      panic("uvmcopy(debug): blank pte");
    }
    if((*pte & PTE_V) == 0) {
      printf("b");
      panic("uvmcopy(debug): pte not in use");
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    if (pa >= SUPER_PAGE_START) {
      szinc = SUPERPGSIZE;
      if ((mem = salloc()) == 0) {
              printf("d");

        goto err;
      }
    } else {
      szinc = PGSIZE;
      if ((mem = kalloc()) == 0) {
              printf("e");

        goto err;
      }
    }

    memmove(mem, (char*)pa, szinc);
    if(mappages(new, i, szinc, (uint64)mem, flags) != 0){
      if (szinc == SUPERPGSIZE)
        sfree(mem);
      else
        kfree(mem);
            printf("f");

      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }


    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
    
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;
  
  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}




// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();
  

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if(ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64) kalloc();
  if(mem == 0)
    return 0;
  memset((void *) mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}



#ifdef LAB_PGTBL
pte_t*
pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif
