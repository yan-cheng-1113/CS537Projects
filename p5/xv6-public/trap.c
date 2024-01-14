#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "mmap.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// void handle_page_fault(struct trapframe *tf) {
//   struct proc *p = myproc();
//   uint page_fault_addr = rcr2();
//   for (int i = 0; i < p->total_vmas; i++) {
//     uint start = p->vmas[i].addr;
//     uint end = start + p->vmas[i].len;
//     if (page_fault_addr >= start && page_fault_addr <= end) {
//       pde_t *pte;
//       if (get_physical_page(p, PGROUNDDOWN(page_fault_addr), &pte) != 0) {
//         cprintf("Segmentation Fault: %p\n", rcr2());
//         myproc()->killed = 1;
//         return;
//       }
//       uint remsize = p->vmas[i].len - p->vmas[i].stored_size;
//       int size = PGSIZE > remsize ? remsize : PGSIZE;
//       if (mmap_store_data(p, PGROUNDDOWN(page_fault_addr), size,
//                           p->vmas[i].flags, p->vmas[i].prot,
//                           p->vmas[i].file, p->vmas[i].offset + PGROUNDDOWN(page_fault_addr) - p->vmas[i].addr) < 0) {
//         myproc()->killed = 1;
//       }
//       p->vmas[i].stored_size += PGSIZE;
//       return;
//     }
//   }
//   cprintf("Segmentation Fault: %p\n", rcr2());
//   myproc()->killed = 1;
// }

void
do_map(struct proc* p, uint fault_addr, struct vm_area_struct vma)
{
  char *mem;
  mem = kalloc();
  fault_addr = PGROUNDDOWN(fault_addr);
  if (mem == 0){
    p->killed = 1;
    cprintf("tflag4\n");
    return;
  } 
  memset(mem, 0, PGSIZE);
  if(vma.prot & PROT_WRITE){
        if(mappages(p->pgdir, (void*)fault_addr, PGSIZE, V2P(mem), PTE_W|PTE_U) <  0){
          cprintf("out of memory\n");
          p->killed = 1;
          cprintf("tflag5\n");
          return;
        }
      }else{
        if(mappages(p->pgdir, (void*)fault_addr, PGSIZE, V2P(mem), PTE_U) <  0){
          cprintf("out of memory\n");
          p->killed = 1;
          cprintf("tflag6\n");
          return; 
        }
      }
      vma.stored_size += PGSIZE;
      if(!(vma.flags & MAP_ANONYMOUS)){
        int size = PGSIZE;
        if(size > vma.len - vma.stored_size){
          size = vma.len - vma.stored_size;
        }
        fileread(vma.file, (char*)fault_addr, size);
        return;
      }
}

void
pgft_handler(struct trapframe *tf)
{
  struct proc *p = myproc();
  //struct vm_area_struct *vm = 0;
  uint fault_addr = rcr2();
  if (fault_addr >= KERNBASE || fault_addr < MMAPBASE) {
      p->killed = 1;
      cprintf("tflag2\n");
      return;
  }

  for (int i = 0; i < p->total_vmas; i++) {
    cprintf("f addr: %p | addr: %p\n", fault_addr, p->vmas[i].addr);
    if (p->vmas[i].addr <= fault_addr && (p->vmas[i].addr + p->vmas[i].len) >= fault_addr){
      if(walkpgdir(p->pgdir, (void*)fault_addr, 0) != 0 && !(p->vmas[i].flags & MAP_GROWSUP) && !(p->vmas[i].flags & MAP_SHARED)){
        p->killed = 1;
        cprintf("tflag1\n");
        return;
      }

      do_map(p, fault_addr, p->vmas[i]);
      
      return;
    }
  }
  for(int i = 0; i < p->total_vmas; i++){
    if(p->vmas[i].flags & MAP_GROWSUP){
      if(i == p->total_vmas - 1){
        p->vmas[i].len += PGSIZE;
        do_map(p, fault_addr, p->vmas[i]);
        return;
      }
      else{
        if(PGROUNDUP(fault_addr) + PGSIZE < p->vmas[i+1].addr){
          p->vmas[i].len += PGSIZE;
          do_map(p, fault_addr, p->vmas[i]);
          return;
        }
      }
    }
    break;
  }
  cprintf("Segmentation Fault\n");
  myproc()->killed = 1;
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  case T_PGFLT:
    if (rcr2() >= MMAPBASE && rcr2() < KERNBASE) {
      pgft_handler(tf);
      //handle_page_fault(tf);
      break;
    }
  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
