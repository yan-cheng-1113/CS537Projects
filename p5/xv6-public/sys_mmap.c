#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "defs.h"
#include "memlayout.h"
#include "mmap.h"

void zero_mmap_region_struct(struct vm_area_struct *va) {
  va->addr = 0;
  va->len = 0;
  va->flags = 0;
  va->prot = 0;
  va->file = 0;
  va->offset = 0;
  va->flags = 0;
  va->stored_size = 0;
}

int get_physical_page(struct proc *p, int tempaddr, pte_t **pte) {
  *pte = walkpgdir(p->pgdir, (char *)tempaddr, 0);
  if (!*pte) {
    return 0;
  }
  int pa = PTE_ADDR(**pte);
  return pa;
}

void copy_mmap_struct(struct vm_area_struct *mr1, struct vm_area_struct *mr2) {
  mr1->addr = mr2->addr;
  mr1->len = mr2->len;
  mr1->flags = mr2->flags;
  mr1->prot = mr2->prot;
  mr1->file = mr2->file;
  mr1->offset = mr2->offset;
}

int
do_map_helper(struct proc* p, uint size, uint fault_addr, struct vm_area_struct vma)
{
  char *mem;
  mem = kalloc();
  fault_addr = PGROUNDDOWN(fault_addr);
  if (mem == 0){
    p->killed = 1;
    cprintf("tflag4\n");
    return -1;
  } 
  memset(mem, 0, PGSIZE);
  if(vma.prot & PROT_WRITE){
        if(mappages(p->pgdir, (void*)fault_addr, size, V2P(mem), PTE_W|PTE_U) <  0){
          cprintf("out of memory\n");
          p->killed = 1;
          cprintf("tflag5\n");
          return -1;
        }
      }else{
        if(mappages(p->pgdir, (void*)fault_addr, size, V2P(mem), PTE_U) <  0){
          cprintf("out of memory\n");
          p->killed = 1;
          cprintf("tflag6\n");
          return -1; 
        }
      }
      vma.stored_size += size;
      if(!(vma.flags & MAP_ANONYMOUS)){
        int size = PGSIZE;
        if(size > vma.len - vma.stored_size){
          size = vma.len - vma.stored_size;
        }
        fileread(vma.file, (char*)fault_addr, size);
        return 1;
      }
      cprintf("domap fails\n");
      return -1;
}

int copy_maps(struct proc *parent, struct proc *child) {
  pte_t *pte;
  int i = 0;
  while (i < parent->total_vmas) {
    uint virt_addr = parent->vmas[i].addr;
    int protection = parent->vmas[i].prot;
    int isshared = parent->vmas[i].flags & MAP_SHARED;
    uint size = parent->vmas[i].len;
    uint start = virt_addr;
    for (; start < virt_addr + size; start += PGSIZE) {
      uint pa = get_physical_page(parent, start, &pte);
      //cprintf("FLAG!\n");
      if (isshared) {

        // If pa is zero then page is not allocated yet, allocate and continue
        if (pa == 0) {
          int total_mmap_size =
              parent->vmas[i].len - parent->vmas[i].stored_size;
          int size = PGSIZE > total_mmap_size ? total_mmap_size : PGSIZE;
          if(do_map_helper(parent, size, start, parent->vmas[i]) < 0){
            return -1;
          }
        //   parent->vmas[i].stored_size += size;
        }
        pa = get_physical_page(parent, start, &pte);
        // // If the page is shared and then all the data should be stored in page
        // // and mapped to each process
        // char *parentmem = (char *)P2V(pa);
        // if (mappages(child->pgdir, (void *)start, PGSIZE, V2P(parentmem),
        //              protection) < 0) {
        //   // ERROR: Shared mappages failed
        //   cprintf("CopyMaps: mappages failed\n");
        // }
      } 
      else {
        // If the mapping is private, lazy mapping can be done
        if (pa == 0) {
          continue;
        }
        char *mem = kalloc();
        if (!mem) {
          // ERROR: Private kalloc failed
          return -1;
        }
        char *parentmem = (char *)P2V(pa);
        memmove(mem, parentmem, PGSIZE);
        if (mappages(child->pgdir, (void *)start, PGSIZE, V2P(mem),
                     protection) < 0) {
          // ERROR: Private mappages failed
          return -1;
        }
      }
    }
    copy_mmap_struct(&child->vmas[i], &parent->vmas[i]);
    if (isshared) {
      child->vmas[i].ref_count = 1;
    }
    i += 1;
  }
  child->total_vmas = parent->total_vmas;
  return 0;
}

int setup_mmap_arr(struct proc *p, int size, int i, int mmapaddr) {
  int j = p->total_vmas;
  while (j > i + 1) {
    copy_mmap_struct(&p->vmas[j], &p->vmas[j - 1]);
    j--;
  }
  if (PGROUNDUP(mmapaddr + size) >= KERNBASE) {
    // Address Exceeds KERNBASE
    return -1;
  }
    p->vmas[i + 1].addr = mmapaddr;
    p->vmas[i + 1].len = size;
  return i + 1; // Return the index of mmap mapping
}

int check_mmap_possible(struct proc *p, int addr, int size) {
  int mmap_addr = PGROUNDUP(addr);
  if (mmap_addr > PGROUNDUP(p->vmas[p->total_vmas - 1].addr +
                            p->vmas[p->total_vmas - 1].len)) {
    return setup_mmap_arr(p, size, p->total_vmas-1, mmap_addr);
  }
  int i = 0;
  for (; i < p->total_vmas - 1; i++) {
    if (p->vmas[i].addr >= mmap_addr) {
      return -1;
    }
    int start_addr = PGROUNDUP(p->vmas[i].addr + p->vmas[i].len);
    int end_addr = PGROUNDUP(p->vmas[i + 1].addr);
    if (mmap_addr > start_addr && end_addr > mmap_addr + size) {
      return setup_mmap_arr(p, size, i, mmap_addr);
    }
  }
  return -1;
}

int find_mmap_addr(struct proc *p, int size) {
  if (p->total_vmas == 0) {
    if (PGROUNDUP(MMAPBASE + size) >= KERNBASE) {
      // Address Exceeds KERNBASE
      return -1;
    }
    p->vmas[0].addr = PGROUNDUP(MMAPBASE);
    p->vmas[0].len = size;
    return 0; // Return the index in mmap region array
  }
  int i = 0;
  uint mmapaddr;
  // If mapping is possible between MMAPBASE & first mapping
  if (p->vmas[0].addr - MMAPBASE > size) {
    mmapaddr = MMAPBASE;
    return setup_mmap_arr(p, size, -1, mmapaddr);
  }
  // Find the map address
  while (i < p->total_vmas && p->vmas[i + 1].addr != 0) {
    uint start_addr = PGROUNDUP(p->vmas[i].addr + p->vmas[i].len);
    uint end_addr = PGROUNDUP(p->vmas[i + 1].addr);
    if (end_addr - start_addr > size) {
      break;
    }
    i += 1;
  }
  mmapaddr = PGROUNDUP(p->vmas[i].addr + p->vmas[i].len);
  if (mmapaddr + size > KERNBASE) {
    return -1;
  }
  // Right shift the mappings to arrange in increasing order
  return setup_mmap_arr(p, size, i, mmapaddr);
}

void *ummap(int addr, int length, int prot, int flags, int offset, struct file* file)
{
  struct proc *p = myproc();
  if(p->total_vmas >= 32){
    cprintf("flag1\n");
    return (void*) -1;
  }

  if (!(flags & MAP_PRIVATE) && !(flags & MAP_SHARED)) {
    return (void *)-1;
  }

  int rnd_addr = PGROUNDUP(PGROUNDUP(addr) + length);
  int i = -1;
  if(flags & MAP_FIXED){
    cprintf("%p\n", addr);
    if((void*)addr == 0 || addr < MMAPBASE || rnd_addr > KERNBASE){
      cprintf("flag2\n");
      return (void*) -1;
    }
    if(rnd_addr > KERNBASE || rnd_addr % PGSIZE != 0){
      cprintf("flag3\n");
      return (void*) -1;
    }
    i = check_mmap_possible(p, addr, length);
    if(i == -1){
      cprintf("flag4\n");
      return (void*) -1;
    }
  }
  else{
    int temp = 0;
    if(rnd_addr > KERNBASE){
      cprintf("flag5\n");
      return (void*) -1;
    }
    i = check_mmap_possible(p, addr, length); 
    if(i != -1){
      temp = 1;
    }
    if(temp == 0){
      i = find_mmap_addr(p, length);
    }
    if(i == -1){
      cprintf("flag6\n");
      return (void*) -1;
    }
  }
  p->vmas[i].file = file;
  p->vmas[i].flags = flags;
  p->vmas[i].prot = PTE_U | prot;
  p->vmas[i].offset = offset;
  p->total_vmas++;
  //cprintf("%d\n", p->vmas[i].addr);
  return (void *)p->vmas[i].addr;
}

int umunmap(struct proc *p, int addr, int length)
{
  //int end = PGROUNDUP(addr + length); 
  //struct proc *p = myproc();
  pte_t *pte;
  uint mainaddr = PGROUNDUP(addr);
  int unmapping_size = PGROUNDUP(length);
  int i = 0;
  int total_size = 0;
  // Find the mmap entry
  for (; i < 32; i++) {
    if (p->vmas[i].addr == mainaddr) {
      total_size = p->vmas[i].len;
      break;
    }
  }
  // Page with given address does not exist
  if (i == 32 || total_size == 0) {
    // Addr not present in mappings
    return -1;
  }
  uint isanon = p->vmas[i].flags & MAP_ANONYMOUS;
  uint isshared = p->vmas[i].flags & MAP_SHARED;
  if (isshared && !isanon && (p->vmas[i].prot & PROT_WRITE)) {
    // write into the file
    p->vmas[i].file->off = p->vmas[i].offset;
    if (filewrite(p->vmas[i].file, (char *)p->vmas[i].addr,
                  p->vmas[i].len) < 0) {
      // File write failed
      return -1;
    }
  }
  // Free the allocated page
  int currsize = 0;
  int main_map_size = unmapping_size > total_size ? total_size: unmapping_size;
  for (; currsize < main_map_size; currsize += PGSIZE) {
    uint tempaddr = addr + currsize;
    uint pa = get_physical_page(p, tempaddr, &pte);
    if (pa == 0) {
      // Page was not mapped yet
      continue;
    }
    char *v = P2V(pa);
    kfree(v);
    *pte = 0;
  }
  if (p->vmas[i].len <= unmapping_size) {
    zero_mmap_region_struct(&p->vmas[i]);
    // Left shift the mmap array
    while (i < 32 && p->vmas[i + 1].addr) {
      copy_mmap_struct(&p->vmas[i], &p->vmas[i + 1]);
      i += 1;
    }
    p->total_vmas -= 1;
  } else {
    p->vmas[i].addr += unmapping_size;
    p->vmas[i].len -= unmapping_size;
  }
  return 0;
}

void clear_maps(struct proc *p){
  int total = p->total_vmas;
  while(total > 0){
    if(p->vmas[p->total_vmas-1].ref_count == 0){
      umunmap(p, p->vmas[p->total_vmas-1].addr, p->vmas[p->total_vmas-1].len); 
    }
    total --;
  }
  p->total_vmas = 0;
}