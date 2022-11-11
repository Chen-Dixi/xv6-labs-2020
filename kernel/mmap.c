#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"

struct {
  struct spinlock lock;
  struct vma vma[NVMA];
} vmatable;


// 从整个系统的vmatbale中选一个还没被占用的vma，返回给他进行记录
// va 是 page-aligned
struct vma*
vmaalloc()
{
  struct vma* vma;
  acquire(&vmatable.lock);
  for(vma = vmatable.vma; vma < vmatable.vma + NVMA; vma++) {
    if (vma->valid == 0) {
      vma->valid = 1;
      release(&vmatable.lock);    
      return vma;
    }
  }

  release(&vmatable.lock);
  return 0;
}

void vmaclose(struct vma *ma) {
  acquire(&vmatable.lock);
  fileclose(ma->fp);
  ma->fp = 0;
  ma->addr = 0;
  ma->flags = 0;
  ma->prot = 0;
  ma->length = 0;
  ma->offset = 0;
  release(&vmatable.lock);
}

// 每一页内存，全部用来映射同一文件。
// return -1 on error
uint64
mmap(struct file* f, int length, int prot, int flags, int offset)
{
  uint64 sz;
  struct proc *p = myproc();
  uint64 oldsz = p->sz;

  oldsz = PGROUNDUP(oldsz);
  if ((prot & PROT_WRITE) && (flags & MAP_SHARED) && !f->writable) {
    // 不可写的文件，不能以PROT_WRITE和 MAP_SHARED方式进行映射
    return -1;
  }

  // 页表项标志位 PTE_FLAG
  // 是否可写，是否可执行，是否可读
  int perm = (prot & PROT_WRITE ? PTE_W : 0) | (prot & PROT_EXEC ? PTE_X : 0) |  (prot & PROT_READ ? PTE_R : 0);
  
  // 映射一个文件，懒分配一整块内存页作为mmap区域。 PTE_MMAP表示这一页内存都是用来做mmap的，懒分配时，perm不包含PTE_V
  if ((sz = uvmalloc_lazy(p->pagetable, oldsz, oldsz + length, PTE_MMAP|perm|PTE_U)) == 0) {
    return 0;
  }

  p->sz = PGROUNDUP(sz); // All 4096 Bytes of a page should be used for one mmaped-file。

  return oldsz;
}

// ip: inode pointer
// 读取文件中的4096个字节，拷贝到va所在的地址。内存中应该保留一整页的数据
int
test_mmapread(struct vma* vma, uint64 va)
{
  uint64 va_down;
  uint off;
  struct file* f = vma->fp;
  va_down = PGROUNDDOWN(va);
  // n = PGROUNDUP(va) - va;
  off = va_down - vma->addr + vma->offset;
  ilock(f->ip);
  // 读一整个page
  if (readi(f->ip, 1, va_down, off, PGSIZE) < 0) {
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);
  return 0;
}

// 更新vma->addr 或 vma->length
// addr 和 length 不需要满足 page-aligned
int
test_munmap(struct vma* vma, uint64 addr, int length) {
  acquire(&vmatable.lock);
  
  uint64 vma_addr = vma->addr;
  int vma_length = vma->length;
  uint64 vma_end = vma_addr + vma_length;
  uint64 va0, n;
  pte_t* pte;
  struct proc* p = myproc();
  
  // 1 边界判断
  if (addr + length < addr)
    panic("test_munmap: uint64 overflow");

  if (addr < vma_addr || (addr > vma_addr && addr + length < vma_end) || addr >= vma_end || addr + length > vma_end)
    // punch a hole in the middle of the mmaped-region
    panic("test_munmap: invalid addr range");
  
  // 2 更新vma里的内容
  if (addr == vma_addr) {
    // unmap at the start
    vma->addr = addr + length;
    vma->length = vma_end - vma->addr;
    vma->offset += length;
  } else if (addr + length == vma_addr + vma_end) {
    // unmap at the end
    vma->length = vma_length - length;
  }

  // 3 更新内存page
  // unmap一整个page的话，根据flags写回file
  while (length > 0)
  {
    /* code */
    va0 = PGROUNDDOWN(addr);
    n = PGSIZE - (addr - va0);
    if (n > length)
      n = length;
    
    if ((pte = walk(p->pagetable, va0, 0)) == 0)
        panic("test_munmap: walk");
    
    if (PTE_FLAGS(*pte) == PTE_V) {
        panic("test_munmap: not leaf");
    }

    if(*pte & PTE_V) {
        
    }
    
    
    length -= n;
    addr = va0 + PGSIZE;
  }

  if (vma->length == 0) {
    // munmap removes all pages of a previous mmap

  }
  
  release(&vmatable.lock);
  return 0;
}

int filemap_sync(struct vma* vma, uint64 va, uint64 pa)
{
    return 0;
}