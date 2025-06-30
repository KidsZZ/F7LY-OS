#ifdef LOONGARCH
#include "types.hh"
#include "trap.hh"
#include "platform.hh"
#include "param.h"
// #include "plic.hh"
#include "mem.hh"
#include "mem/memlayout.hh"
#include "devs/console.hh"
#include "printer.hh"
#include "proc/proc.hh"
#include "proc/proc_manager.hh"
#include "proc/scheduler.hh"
#include "trap_func_wrapper.hh"
#include "extioi.hh"
#include "pci.h"
#include "apic.hh"
#include "syscall_handler.hh"
#include "cpu.hh"
#include "physical_memory_manager.hh"
#include "virtual_memory_manager.hh"
#include "vfs/file/normal_file.hh"
#include "devs/loongarch/disk_driver.hh"
// in kernelvec.S, calls kerneltrap().
extern "C" void kernelvec();
extern "C" void uservec();
extern "C" void handle_tlbr();
extern "C" void handle_merr();
extern "C" void userret(uint64, uint64);
int mmap_handler(uint64 va, int cause);
// 创建一个静态对象
trap_manager trap_mgr;

// 初始化锁
void trap_manager::init()
{
  ticks = 0;
  timeslice = 0;
  tickslock.init("tickslock");
  printfGreen("[trap] Trap Manager Init\n");
}

// 架构相关, 设置csr
void trap_manager::inithart()
{
  uint32 ecfg = (0U << CSR_ECFG_VS_SHIFT) | HWI_VEC | TI_VEC;
  uint64 tcfg = 0x1000000UL | CSR_TCFG_EN | CSR_TCFG_PER;

  w_csr_ecfg(ecfg);
  w_csr_tcfg(tcfg);

  w_csr_eentry((uint64)kernelvec);
  w_csr_tlbrentry((uint64)handle_tlbr);
  w_csr_merrentry((uint64)handle_merr);
  intr_on();
}

// 处理外部中断和软件中断
int trap_manager::devintr()
{

  uint32 estat = r_csr_estat();
  uint32 ecfg = r_csr_ecfg();

  if (estat & ecfg & HWI_VEC)
  {
    // this is a hardware interrupt, via IOCR.

    // irq indicates which device interrupted.
    uint64 irq = extioi_claim();
    // printf("%d\n", irq);
    // 处理串口中断
    if (irq & (1UL << UART0_IRQ))
    {
      TODO("处理串口中断");
      panic("处理串口中断未实现");
      // int c = sbi_console_getchar();
      // if (-1 != c)
      // {
      //   dev::kConsole.console_intr(c);
      // }

      // tell the apic the device is
      // now allowed to interrupt again.

      extioi_complete(1UL << UART0_IRQ);
    }
    else if (irq & (1UL << PCIE_IRQ))
    {
      loongarch::qemu::disk_driver.handle_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);

      apic_complete(irq);
      extioi_complete(irq);
    }

    return 1;
  }
  else if (estat & ecfg & TI_VEC)
  {
    // timer interrupt,

    if (proc::k_pm.get_cur_cpuid() == 0)
    {
      timertick();
    }

    // acknowledge the timer interrupt by clearing
    // the TI bit in TICLR.
    w_csr_ticlr(r_csr_ticlr() | CSR_TICLR_CLR);

    return 2;
  }
  else
  {
    return 0;
  }
}

void trap_manager::timertick()
{
  // acquire the lock to protect ticks
  tickslock.acquire();

  // increment the ticks count
  ticks++;

  // !!写完进城后修改
  proc::k_pm.wakeup(&ticks);

  // release the lock
  tickslock.release();
}

// !!写完进程后修改
void trap_manager::usertrap()
{
  // printfMagenta("==usertrap==\n");

  int which_dev = 0;

  if ((r_csr_prmd() & PRMD_PPLV) == 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_csr_eentry((uint64)kernelvec);

  proc::Pcb *p = proc::k_pm.get_cur_pcb();

  // save user program counter.
  p->_trapframe->era = r_csr_era();

  if (((r_csr_estat() & CSR_ESTAT_ECODE) >> 16) == 0xb)
  {
    // system call

    if (p->_killed)
      proc::k_pm.exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->_trapframe->era += 4;

    // an interrupt will change crmd & prmd registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall::k_syscall_handler.invoke_syscaller();
  }

  else if (((r_csr_estat() & CSR_ESTAT_ECODE) >> 16 == 0x1 || (r_csr_estat() & CSR_ESTAT_ECODE) >> 16 == 0x2))
  {
    // printfRed("p->_trapframe->sp: %p,fault_va: %p,p->sz:%p\n", p->_trapframe->sp, r_csr_badv(), p->_sz);
    if (mmap_handler(r_csr_badv(), (r_csr_estat() & CSR_ESTAT_ECODE) >> 16) != 0)
    {
      printf("usertrap(): unexpected trapcause %x pid=%d\n", r_csr_estat(), p->_pid);
      printf("            era=%p badi=%x\n", r_csr_era(), r_csr_badi());
      p->_killed = 1;
    }
  }
  else if ((which_dev = devintr()) != 0)
  {
    // ok
  }
  else
  {
    printf("usertrap(): unexpected trapcause %x pid=%d\n", r_csr_estat(), p->_pid);
    printf("            era=%p badi=%x,badv=%p\n", r_csr_era(), r_csr_badi(), r_csr_badv());
    p->_killed = 1;
  }

  if (p->_killed)
    proc::k_pm.exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
  {
    timeslice++; // 让一个进程连续执行若干时间片，printf线程不安全
    if (timeslice >= 10)
    {
      timeslice = 0;
      // 处理信号 - 在返回用户态之前检查并处理待处理的信号
      proc::ipc::signal::handle_signal();
      printf("yield in usertrap\n");
      proc::k_scheduler.yield();
    }
  }

  
  
  usertrapret();
}

void trap_manager::usertrapret(void)
{
//   printfCyan("==usertrapret== pid=%d\n", proc::k_pm.get_cur_pcb()->_pid);
  proc::Pcb *p = proc::k_pm.get_cur_pcb();

  // 检查进程是否使用共享虚拟内存，如果是则需要动态映射trapframe
  if (p->_shared_vm || p->_pt.get_ref_count() > 1) {
    // 页表被共享，需要动态重新映射trapframe
    printfCyan("[usertrapret] Page table shared (ref count: %d), dynamically mapping trapframe for pid %d\n", 
               p->_pt.get_ref_count(), p->_pid);
    
    // 取消当前trapframe的映射
    mem::k_vmm.vmunmap(p->_pt, TRAPFRAME, 1, 0);
    
    // 重新映射当前进程的trapframe
    if (mem::k_vmm.map_pages(p->_pt, TRAPFRAME, PGSIZE, (uint64)(p->_trapframe), 
                             PTE_V | PTE_NX | PTE_P | PTE_W | PTE_R | PTE_MAT | PTE_D) == 0)
    {
      panic("usertrapret: failed to dynamically map trapframe");
    }
  }

  intr_off();

  // send syscalls, interrupts, and exceptions to uservec.S
  w_csr_eentry((uint64)uservec); // maybe todo

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->_trapframe->kernel_pgdl = r_csr_pgdl();      // kernel page table
  p->_trapframe->kernel_sp = p->_kstack + PGSIZE; // process's kernel stack
  p->_trapframe->kernel_trap = (uint64)wrap_usertrap;
//   printf("usertrapret: p->_trapframe->kernel_trap: %p\n", p->_trapframe->kernel_trap);
  p->_trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

  // set up the registers that uservec.S's ertn will use
  // to get to user space.

  // set Previous Privilege mode to User Privilege3.
  uint32 x = r_csr_prmd();
  x |= PRMD_PPLV; // set PPLV to 3 for user mode
  x |= PRMD_PIE;  // enable interrupts in user mode
  w_csr_prmd(x);

  // set S Exception Program Counter to the saved user pc.
  w_csr_era(p->_trapframe->era);

  // tell uservec.S the user page table to switch to.
  volatile uint64 pgdl = (p->_pt.get_base());

  // jump to uservec.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with ertn.
  userret(TRAPFRAME, pgdl);
}
void trap_manager::machine_trap()
{
  panic("machine error");
}
// 处理内核态的中断
// 支持嵌套中断
void trap_manager::kerneltrap()
{
  // printf("==kerneltrap==\n");
  // 这些寄存器可能在yield时被修改
  int which_dev = 0;
  uint64 era = r_csr_era();
  uint64 prmd = r_csr_prmd();

  // 检查中断是否来自内核态

  if ((prmd & PRMD_PPLV) != 0)
    panic("kerneltrap: not from privilege0");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0)
  {
    printf("estat %x\n", r_csr_estat());
    printf("era=%p eentry=%p\n", r_csr_era(), r_csr_eentry());
    panic("kerneltrap");
  }

  ///@todo!! 写完进程后修改
  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && Cpu::get_cpu()->get_cur_proc() != nullptr && Cpu::get_cpu()->get_cur_proc()->_state == proc::RUNNING)
  {
    timeslice++; // 让一个进程连续执行若干时间片，printf线程不安全
    if (timeslice >= 5)
    {
      timeslice = 0;
      printf("yield in kerneltrap\n");
      proc::k_scheduler.yield();
    }
  }
  if (which_dev == 2)
  {
    timeslice++; // 让一个进程连续执行若干时间片，printf线程不安全
    // printf("timeslice: %d\n", timeslice);
    if (timeslice >= 5)
    {
      timeslice = 0;
    }
  }

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_csr_era(era);
  w_csr_prmd(prmd);
}
int mmap_handler(uint64 va, int cause)
{
  int i;
  proc::Pcb *p = proc::k_pm.get_cur_pcb();
  // 根据地址查找属于哪一个VMA
  for (i = 0; i < proc::NVMA; ++i)
  {
    if (p->_vma->_vm[i].used)
    {

      // 检查是否在当前VMA范围内
      if (va >= p->_vma->_vm[i].addr && va < p->_vma->_vm[i].addr + p->_vma->_vm[i].len)
      {
        break; // 在当前VMA范围内
      }
      // 检查是否可以扩展
      else if (p->_vma->_vm[i].is_expandable &&
               va < p->_vma->_vm[i].addr + p->_vma->_vm[i].max_len)
      {
        // 扩展当前VMA
        uint64 old_len = p->_vma->_vm[i].len;
        uint64 new_len = PGROUNDUP(va - p->_vma->_vm[i].addr + PGSIZE);
        if (new_len <= p->_vma->_vm[i].max_len)
        {
          p->_vma->_vm[i].len = new_len;
          p->_sz += (new_len - old_len);
          printfCyan("VMA expanded from %d to %d bytes, p->_sz now %p\n", old_len, new_len, p->_sz);
          break;
        }
      }
    }
  }
  if (i == proc::NVMA)
  {
    printfRed("mmap_handler: no suitable VMA found for va %p\n", va);
    return -1;
  }
  // 检查该页面是否已经映射
  // mem::Pte existing_pte = p->get_pagetable()->walk(va, false);
  // if (!existing_pte.is_null() && existing_pte.is_valid())
  // {
  //   // 页面已经映射，不需要重复处理
  //   return 0;
  // }
  // printfCyan("mmap_handler: handling mmap at %p, cause: %d\n", va, cause);
  int pte_flags = PTE_U |  PTE_P | PTE_D | PTE_MAT;
  
  // 特殊处理 prot=0 的情况
  if (p->_vma->_vm[i].prot == 0) {
    // 对于 prot=0 的映射，在 LoongArch 上只给予用户态访问权限
    // 不设置 PTE_NR 和 PTE_NX，使其可读可执行但不可写
    printfYellow("mmap_handler: prot=0 mapping, using minimal permissions\n");
  } else {
    // 正常的权限处理
    if (!(p->_vma->_vm[i].prot & PROT_READ))
      pte_flags |= PTE_NR;
    if (p->_vma->_vm[i].prot & PROT_WRITE)
      pte_flags |= PTE_W;
    if (!(p->_vma->_vm[i].prot & PROT_EXEC))
      pte_flags |= PTE_NX;
  }

  fs::normal_file *vf = p->_vma->_vm[i].vfile;

  void *pa = mem::k_pmm.alloc_page();

  if (pa == 0)
    return -1;
  memset(pa, 0, PGSIZE);

  // 读取文件内容
  if (vf == nullptr || p->_vma->_vm[i].vfd == -1)
  {
    // 匿名映射：页面已经初始化为0，直接映射即可
    // printfCyan("mmap_handler: handling anonymous mapping at %p\n", va);
  }
  else
  {
    fs::dentry *den = vf->getDentry();
    if (den == nullptr)
    {
      printfRed("mmap_handler: dentry is null\n");
      mem::k_pmm.free_page(pa);
      return -1; // dentry is null
    }
    fs::Inode *inode = den->getNode();
    if (inode == nullptr)
    {
      printfRed("mmap_handler: inode is null\n");
      mem::k_pmm.free_page(pa);
      return -1; // inode is null
    }

    inode->_lock.acquire(); // 锁定inode，防止其他线程修改

    // 计算当前页面读取文件的偏移量，实验中p->_vma->_vm[i].offset总是0
    // 要按顺序读读取，例如内存页面A,B和文件块a,b
    // 则A读取a，B读取b，而不能A读取b，B读取a
    int offset = p->_vma->_vm[i].offset + PGROUNDDOWN(va - p->_vma->_vm[i].addr);
    ///@details 原本的xv6的readi函数有一个标志位来区分是否读到内核中，此处位于内核里
    /// pa直接是物理地址，所以应该无所谓
    int readbytes = inode->nodeRead((uint64)pa, offset, PGSIZE);

    // 什么都没有读到
    if (readbytes == 0)
    {
      printfRed("mmap_handler: read nothing");
      inode->_lock.release();
      mem::k_pmm.free_page(pa);
      return -1;
    }
    inode->_lock.release(); // 释放inode锁
  }
  // 添加页面映射
  printfCyan("mmap_handler: mapping page at %p to %p with flags %p\n", va, pa, pte_flags);
  if (mem::k_vmm.map_pages(*p->get_pagetable(), PGROUNDDOWN(va), PGSIZE, (uint64)pa, pte_flags) != 1)
  {
    printfRed("mmap_handler: map failed");
    mem::k_pmm.free_page(pa);
    return -1;
  }

  // 在 LoongArch 上刷新 TLB
  asm volatile("invtlb 0x5, $zero, %0" : : "r"(PGROUNDDOWN(va)) : "memory");
  
  // printfCyan("mmap_handler: successfully mapped and returning\n");
  return 0;
}
#endif