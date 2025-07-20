//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

int
vmaread(struct VMA *vma, uint64 va, uint64 pa)
{
  va = PGROUNDDOWN(va);
  if(vma->f->type != FD_INODE){
    printf("vmaread: file type error\n");
    return -1;
  }

  ilock(vma->f->ip);
  if(readi(vma->f->ip, 0, pa, va - vma->start, PGSIZE) < 0){
    printf("vmaread: readi failed\n");
    iunlock(vma->f->ip);
    return -1;
  }
  iunlock(vma->f->ip);

  return 0;
}

int
vmawrite_helper(struct file *f, int user_src, uint64 addr, uint64 off, uint64 n)
{
  int r;

  // printf("helper: %d %p %p %p\n", user_src, addr, off, n);
  begin_op();
  ilock(f->ip);

  r = writei(f->ip, user_src, addr, off, n);

  iunlock(f->ip);
  end_op();
  return r;
}

int
vmaunmap(struct VMA *vma, uint64 addr, uint64 length, int writeback)
{
  if(addr < vma->start || addr + length > vma->end)
    panic("vmaunmap: beyond range\n");

  // printf("vmaunmap: %p %p %d\nvma: %p %p\n", addr, length, writeback, vma->start, vma->end);
  uint64 start = PGROUNDUP(addr), end = PGROUNDDOWN(addr + length);
  uint64 pa;
  struct proc *p;

  p = myproc();
  if(writeback && vmawrite_helper(vma->f, 1, start, 0, end - start) < 0){
    return -1;
  }
  // printf("vmaunmap: point0\n");
  uvmunmap(p->pagetable, start, (end - start) / PGSIZE, 1);

  // printf("vmaunmap: point1\n");
  if(start > addr){
    if((pa = walkaddr(p->pagetable, addr)) == 0){
      return -1;
    }

    if(writeback && vmawrite_helper(vma->f, 0, pa, addr & ((1 << PGSHIFT) - 1), start - addr) < 0){
      return -1;
    }
    memset((void *)pa + (addr & ((1 << PGSHIFT) - 1)), 0, start - addr);
  }

  // printf("vmaunmap: point2\n");
  if(end < addr + length){
    if((pa = walkaddr(p->pagetable, end)) == 0){
      return -1;
    }

    if(writeback && vmawrite_helper(vma->f, 0, pa, 0, addr - end + length) < 0){
      return -1;
    }
    memset((void *)pa, 0, addr - end + length);
    if(addr + length == vma->end){
      uvmunmap(p->pagetable, end, 1, 1);
    }
  }

  // printf("vmaunmap: point3\n");
  if(addr + length == vma->end){
    vma->end -= length;
  }
  if(addr == vma->start){
    vma->start += length;
  }

  if(vma->start >= vma->end){
    fileclose(vma->f);
    vma->valid = 0;
  }

  return 0;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

