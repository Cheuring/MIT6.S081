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
int dec_pageref(uint64 pa);
void _kfree(void *pa);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define BUCKSZ 997
#define PPN(pa) ((((uint64)pa) - KERNBASE) >> PGSHIFT)
struct {
  struct spinlock locks[BUCKSZ];
  int ref[(PHYSTOP - KERNBASE) >> PGSHIFT];
} pref;

void
kinit()
{
  for(int i=0; i<BUCKSZ; ++i)
    initlock(&pref.locks[i], "ref_lock");
  memset(pref.ref, 0, sizeof(pref.ref));

  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    _kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
_kfree(void *pa)
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
void
kfree(void *pa)
{
  struct run *r;
  int cnt;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  cnt = dec_pageref((uint64)pa);
  if(cnt < 0)
    panic("kfree: over dec\n");
  if(cnt > 0) return;

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
    inc_pageref((uint64)r);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}


void
inc_pageref(uint64 pa)
{
  uint64 ppn = PPN(pa);
  acquire(&pref.locks[ppn % BUCKSZ]);
  ++pref.ref[ppn];
  release(&pref.locks[ppn % BUCKSZ]);
}

int
dec_pageref(uint64 pa)
{
  uint64 ppn = PPN(pa);
  int res;
  acquire(&pref.locks[ppn % BUCKSZ]);
  res = --pref.ref[ppn];
  release(&pref.locks[ppn % BUCKSZ]);

  return res;
}

int
handle_cow(pte_t* pte)
{
  uint64 pa;
  int cnt;
  void* mem;
  if((*pte & PTE_C) == 0)
    panic("handle_cow: pte not cow");

  pa = PTE2PA(*pte);

  cnt = dec_pageref(pa);
  if(cnt > 0){
    if((mem = kalloc()) == 0){
      inc_pageref(pa);
      printf("handle_cow: out of mem\n");
      return -1;
    }

    memmove(mem, (void*)pa, PGSIZE);
    *pte = PA2PTE(mem) | PTE_FLAGS(*pte);
  }else if(cnt == 0){
    inc_pageref(pa);
  }else{
    panic("handle_cow: over dec");
  }
  *pte = (*pte & ~PTE_C) | PTE_W;

  return 0;
}