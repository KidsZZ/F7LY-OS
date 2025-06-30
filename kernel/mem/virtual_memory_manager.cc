#include "klib.hh"
#include "virtual_memory_manager.hh"
#include "physical_memory_manager.hh"
#ifdef RISCV
#include "mem/riscv/pagetable.hh"
#elif defined(LOONGARCH)
#include "mem/loongarch/pagetable.hh"
#endif
#include "memlayout.hh"
#include "platform.hh"
#include "printer.hh"
#include "proc/proc.hh"
#include "proc_manager.hh"
extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S
#ifdef LOONGARCH
void tlbinit(void)
{
    asm volatile("invtlb  0x0,$zero,$zero");
    w_csr_stlbps(0xcU);
    w_csr_asid(0x0U);
    w_csr_tlbrehi(0xcU);
}
#endif
namespace mem
{
    VirtualMemoryManager k_vmm;

    uint64 VirtualMemoryManager::kstack_vm_from_gid(uint gid)
    {
        if (gid >= proc::num_process)
            panic("vmm: invalid gid");
        return (TRAPFRAME - (((gid + 1) * 2) << PGSHIFT));
    }

    void VirtualMemoryManager::init(const char *lock_name)
    {

        _virt_mem_lock.init(lock_name);
        // 创建内核页表
        k_pagetable = kvmmake();
        // for(uint64 va = KERNBASE; va < (uint64)etext; va += PGSIZE)
        // {
        //     uint64 ppp= (uint64)k_pagetable.walk_addr(va);
        //     printfRed("va: %p, pa: %p\n", va, ppp);
        // }
        // TODO
        for (proc::Pcb &pcb : proc::k_proc_pool)
        {
            pcb.map_kstack(k_pagetable);
        }
#ifdef RISCV
        // 设置satp，对应龙芯应该设置pgdl，pgdh，stlbps，asid，tlbrehi，pwcl，pwch,
        // 并且invtlb 0x0,$zero,$zero;
        // question: 为什么xv6的MAKE_SATP没有设置asid

        sfence_vma();
        // printfYellow("sfence\n");
        w_satp(MAKE_SATP(k_pagetable.get_base()));
        // printfYellow("sfence\n");
        sfence_vma();
#elif defined(LOONGARCH)

        // the "pgdl" is corresponding to "satp" in riscv
        w_csr_pgdl((uint64)k_pagetable.get_base());
        // flush the tlb(tlbinit)
        tlbinit();

        w_csr_pwcl((PTEWIDTH << 30) | (DIR2WIDTH << 25) | (DIR2BASE << 20) | (DIR1WIDTH << 15) | (DIR1BASE << 10) | (PTWIDTH << 5) | (PTBASE << 0));
        w_csr_pwch((DIR4WIDTH << 18) | (DIR3WIDTH << 6) | (DIR3BASE << 0) | (PWCH_HPTW_EN << 24));

        [[maybe_unused]] uint64 crmd = r_csr_crmd();

#endif
        printfGreen("[vmm] Virtual Memory Manager Init\n");
    }

    // 根据传入的 flags 标志，生成对应的页表权限（perm）值
    bool VirtualMemoryManager::map_pages(PageTable &pt, uint64 va, uint64 size, uint64 pa, uint64 flags)
    {
        // printf("map_pages: va=0x%x, size=0x%x, pa=0x%x, flags=0x%x\n", va, size, pa, flags);

        uint64 a, last;
        Pte pte;

        if (size == 0)
            panic("mappages: size");

        a = PGROUNDDOWN(va);

        last = PGROUNDDOWN(va + size - 1);

        for (;;)
        {
            // printfMagenta("map_pages: va=0x%x, size=0x%x, pa=0x%x, flags=0x%x\n", a, size, pa, flags);
            pte = pt.walk(a, /*alloc*/ true);
            // printfCyan("walk: va=0x%x, pte_addr=%p, pte_data=%p\n", a, pte.get_data(), pte.get_data());
            // DEBUG:
            //  if(va == KERNBASE)
            //  {
            //      pte = pt.walk(a, false);
            //  }

            if (pte.is_null())
            {
                printfRed("walk failed");
                return false;
            }
            if (pte.is_valid())
                panic("mappages: remap, va=0x%x, pa=0x%x, PteData:%x", a, pa, pte.get_data());
#ifdef RISCV
            pte.set_data(PA2PTE(PGROUNDDOWN(riscv::virt_to_phy_address(pa))) |
                         flags |
                         riscv::PteEnum::pte_valid_m);
#elif defined(LOONGARCH)
            pte.set_data(PA2PTE(PGROUNDDOWN(pa)) |
                         flags |
                         loongarch::pte_valid_m);
            // printfBlue("pa: %p, pte2pa: %p\n", pa, pte.pa());
#endif
            // printfMagenta("由map_page设置的第三级pte: %p,pte_addr:%p，应该是：%p\n", pte.get_data(), pte.get_data_addr(), riscv::virt_to_phy_address(pa));
            // if (pte.get_data_addr() == (uint64*)a)
            // {

            // }
            if (a == last)
                break;
            a += PGSIZE;
            pa += PGSIZE;
        }
        // printfMagenta("map finish for cycle\n");
        return true;
    }

    uint64 VirtualMemoryManager::vmalloc(PageTable &pt, uint64 old_sz, uint64 new_sz, uint64 flags)
    {
#ifdef RISCV
        void *mem;

        if (new_sz < old_sz)
            return old_sz;

        old_sz = PGROUNDUP(old_sz);
        for (uint64 a = old_sz; a < new_sz; a += PGSIZE)
        {
            mem = PhysicalMemoryManager::alloc_page();
            if (mem == nullptr)
            {
                vmdealloc(pt, a, old_sz);
                return 0;
            }
            k_pmm.clear_page(mem);
            if (map_pages(pt, a, PGSIZE, (uint64)mem,
                          riscv::PteEnum::pte_readable_m | flags) == false)
            {
                k_pmm.free_page(mem);
                vmdealloc(pt, a, old_sz);
                return 0;
            }
        }
        return new_sz;
#elif defined(LOONGARCH)
        void *mem;

        if (new_sz < old_sz)
            return old_sz;

        old_sz = PGROUNDUP(old_sz);
        for (uint64 a = old_sz; a < new_sz; a += PGSIZE)
        {
            mem = PhysicalMemoryManager::alloc_page();
            if (mem == nullptr)
            {
                printfRed("vmalloc: alloc_page failed\n");
                vmdealloc(pt, a, old_sz);
                return 0;
            }
            k_pmm.clear_page(mem);
            if (map_pages(pt, a, PGSIZE, (uint64)mem,
                          PTE_R | PTE_U | flags) == false)
            {
                printfRed("vmalloc: map_pages failed\n");
                k_pmm.free_page(mem);
                vmdealloc(pt, a, old_sz);
                return 0;
            }
            // printf("[vmalloc] pt mapping %p", pt.walk_addr(a));;
            // printfCyan("[vmalloc] Successfully mapped VA: %p -> PA: %p\n", a, mem);
        }
        // printfMagenta("vmalloc: old_sz: %p, new_sz: %p\n", old_sz, new_sz);
        return new_sz;

#endif
    }

    uint64 VirtualMemoryManager::vmdealloc(PageTable &pt, uint64 old_sz, uint64 new_sz)
    {
        if (new_sz >= old_sz)
            return old_sz;

        if (PGROUNDUP(new_sz) < PGROUNDUP(old_sz))
        {
            int npages = (PGROUNDUP(old_sz) - PGROUNDUP(new_sz)) / PGSIZE;
            vmunmap(pt, PGROUNDUP(new_sz), npages, true);
        }

        return new_sz;
    }

    /// @brief 从用户空间拷贝数据到内核空间。
    /// @param pt 当前进程的页表，用于地址转换。
    /// @param dst 目标地址（内核空间指针），拷贝到这里。
    /// @param src_va 源地址（用户虚拟地址），从这个地址读取数据。
    /// @param len 拷贝的数据长度（字节数）。
    /// @return 成功返回0，失败返回-1（如页表无法转换用户虚拟地址）。
    int VirtualMemoryManager::copy_in(PageTable &pt, void *dst, uint64 src_va, uint64 len)
    {
        uint64 n, va, pa;
        char *p_dst = (char *)dst;

        while (len > 0)
        {
            va = PGROUNDDOWN(src_va);
            pa = (uint64)pt.walk_addr(va);
            if (pa == 0)
            {
                printfRed("[copyin] pa ==0! walk failed");
                return -1;
            }
            n = PGSIZE - (src_va - va);
            if (n > len)
                n = len;
            memmove((void *)p_dst, (const void *)(pa + (src_va - va)), n);

            len -= n;
            p_dst += n;
            src_va = va + PGSIZE;
        }
        return 0;
    }

    int VirtualMemoryManager::copy_str_in(PageTable &pt, void *dst,
                                          uint64 src_va, uint64 max)
    {
        uint64 n, va, pa;
        int got_null = 0;
        char *p_dst = (char *)dst;

        while (got_null == 0 && max > 0)
        {
            va = PGROUNDDOWN(src_va);
            pa = (uint64)pt.walk_addr(va);
            if (pa == 0)
                return -1;
            n = PGSIZE - (src_va - va);
            if (n > max)
                n = max;

            char *p = (char *)(pa + (src_va - va));
            while (n > 0)
            {
                if (*p == '\0')
                {
                    *p_dst = '\0';
                    got_null = 1;
                    break;
                }
                else
                {
                    *p_dst = *p;
                }
                --n;
                --max;
                p++;
                p_dst++;
            }

            src_va = va + PGSIZE;
        }
        if (got_null)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    }
    int VirtualMemoryManager::copy_str_in(PageTable &pt, eastl::string &dst,
                                          uint64 src_va, uint64 max)
    {
        uint64 n, va, pa;
        int got_null = 0;

        while (got_null == 0 && max > 0)
        {
            va = PGROUNDDOWN(src_va);
            pa = (uint64)pt.walk_addr(va);
            if (pa == 0)
                return -1;
#ifdef RISCV

#elif defined(LOONGARCH)
            pa = to_phy(pa);
#endif
            n = PGSIZE - (src_va - va);
            if (n > max)
                n = max;

            char *p = (char *)(pa + (src_va - va));
            while (n > 0)
            {
                if (*p == '\0')
                {
                    got_null = 1;
                    break;
                }
                else
                {
                    dst.push_back(*p);
                }
                --n;
                --max;
                p++;
            }

            src_va = va + PGSIZE;
        }
        if (got_null)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    }
    // TODO
    // uint64 VirtualMemoryManager::allocshm(PageTable &pt, uint64 oldshm, uint64 newshm, uint64 sz, void *phyaddr[pm::MAX_SHM_PGNUM])
    // {
    //     void *mem;
    //     uint64 a;

    //     if (oldshm & 0xfff || newshm & 0xfff || newshm < sz || oldshm > (vm_trap_frame - 64 * 2 * PGSIZE))
    //     {
    //         panic("allocshm: bad parameters");
    //         return 0;
    //     }
    //     a = newshm;
    //     for (int i = 0; a < oldshm; a += PGSIZE, i++)
    //     {
    //         mem = PhysicalMemoryManager::alloc_page();
    //         if (mem == nullptr)
    //         {
    //             panic("allocshm: no memory");
    //             deallocshm(pt, newshm, a);
    //             return 0;
    //         }
    //         map_pages(pt, a, PGSIZE, uint64(phyaddr[i]), loongarch::PteEnum::presence_m | loongarch::PteEnum::writable_m | loongarch::PteEnum::plv_m | loongarch::PteEnum::mat_m | loongarch::PteEnum::dirty_m);
    //         phyaddr[i] = mem;
    //         printf("allocshm: %p => %p\n", a, phyaddr[i]);
    //     }
    //     return newshm;
    // }
    // TODO
    // uint64 VirtualMemoryManager::mapshm(PageTable &pt, uint64 oldshm, uint64 newshm, uint sz, void **phyaddr)
    // {
    //     uint64 a;
    //     if (oldshm & 0xfff || newshm & 0xfff || newshm < sz || oldshm > (vm_trap_frame - 64 * 2 * PGSIZE))
    //     {
    //         panic("mapshm: bad parameters when shmmap");
    //         return 0;
    //     }
    //     a = newshm;
    //     for (int i = 0; a < oldshm; a += PGSIZE, i++)
    //     {
    //         map_pages(pt, a, PGSIZE, uint64(phyaddr[i]), loongarch::PteEnum::presence_m | loongarch::PteEnum::writable_m | loongarch::PteEnum::plv_m | loongarch::PteEnum::mat_m | loongarch::PteEnum::dirty_m);
    //         printf("mapshm: %p => %p\n", a, phyaddr[i]);
    //     }
    //     return newshm;
    // }

    // uint64 VirtualMemoryManager::deallocshm(PageTable &pt, uint64 oldshm, uint64 newshm)
    // {
    //     if (newshm <= oldshm)
    //         return oldshm;

    //     if (PGROUNDUP(newshm) > PGROUNDUP(oldshm))
    //     {
    //         int npages = PGROUNDUP(newshm) - PGROUNDUP(oldshm) / PGSIZE;
    //         vmunmap(pt, PGROUNDUP(oldshm), npages, 0);
    //     }
    //     return oldshm;
    // }

    /// @brief 从内核地址空间拷贝数据到用户页表映射的虚拟地址空间。
    ///
    /// 将内核中的 `len` 字节数据从指针 `p` 拷贝到用户进程页表 `pt` 所映射的虚拟地址 `va` 起始处，
    /// 自动处理跨页情况。该函数假定目标页表 `pt` 中的虚拟地址 `va` 所需的所有页表项已经建立并映射有效物理页。
    /// 若某一虚拟地址未能映射有效物理页（即 walk 返回的页表项为无效），则返回 -1 表示失败。
    ///
    /// @param pt  用户进程的页表，用于解析虚拟地址。
    /// @param va  拷贝的目标虚拟地址（用户空间），可跨页。
    /// @param p   拷贝的源地址（内核空间指针）。
    /// @param len 拷贝的字节数。
    /// @return 成功返回 0；若任意一页无效或未映射，返回 -1。
    int VirtualMemoryManager::copy_out(PageTable &pt, uint64 va, const void *p, uint64 len)
    {
#ifdef RISCV
        uint64 n, a, pa;
        // printf("[copy_out] va: %p, len: %d\n", va, len);
        // // 打印拷贝的所有字节
        // const uint8_t* p_bytes = reinterpret_cast<const uint8_t*>(p);
        // printf("[copy_out] bytes: ");
        // for (uint64 i = 0; i < len; ++i) {
        //     printf("%02x ", p_bytes[i]);
        // }
        // printf("\n");

        while (len > 0)
        {
            a = PGROUNDDOWN(va);
            bool alloc = false;
            proc::Pcb *proc = proc::k_pm.get_cur_pcb();
            for (int i = 0; i < proc::NVMA; ++i)
            {
                if (proc->_vma->_vm[i].used)
                {

                    // 检查是否在当前VMA范围内
                    if (va >= proc->_vma->_vm[i].addr && va < proc->_vma->_vm[i].addr + proc->_vma->_vm[i].len)
                    {
                        alloc = true;
                        // printfCyan("[copy_out] va: %p is in VMA[%d]: %p-%p\n", va, i, proc->_vma->_vm[i].addr, proc->_vma->_vm[i].addr + proc->_vma->_vm[i].len);
                        break; // 在当前VMA范围内
                    }
                }
            }
            Pte pte = pt.walk(a, 0);
            if (pte.get_data() == 0 && alloc)
            {
                // 如果页表项无效且当前VMA范围内，则分配物理页
                void *mem = k_pmm.alloc_page();
                if (mem == 0)
                {
                    printfRed("[copy_out] alloc page failed for va: %p\n", va);
                    return -1; // 分配失败
                }
                k_pmm.clear_page(mem);
                map_pages(pt, a, PGSIZE, (uint64)mem,
                          riscv::PteEnum::pte_readable_m | riscv::PteEnum::pte_writable_m | riscv::PteEnum::pte_user_m);
            }
            pa = reinterpret_cast<uint64>(pte.pa());
            if (pa == 0)
                return -1;
            n = PGSIZE - (va - a);
            if (n > len)
                n = len;
            memmove((void *)(pa + (va - a)), p, n);

            len -= n;
            p = (char *)p + n;
            va = a + PGSIZE;
        }
        return 0;
#elif defined(LOONGARCH)
        uint64 n, a, pa;
        // printf("[copy_out] va: %p, len: %d\n", va, len);
        // // 打印拷贝的所有字节
        // const uint8_t* p_bytes = reinterpret_cast<const uint8_t*>(p);
        // printf("[copy_out] bytes: ");
        // for (uint64 i = 0; i < len; ++i) {
        //     printf("%02x ", p_bytes[i]);
        // }
        // printf("\n");

        while (len > 0)
        {
            a = PGROUNDDOWN(va);
            bool alloc = false;
            proc::Pcb *proc = proc::k_pm.get_cur_pcb();
            for (int i = 0; i < proc::NVMA; ++i)
            {
                if (proc->_vma->_vm[i].used)
                {

                    // 检查是否在当前VMA范围内
                    if (va >= proc->_vma->_vm[i].addr && va < proc->_vma->_vm[i].addr + proc->_vma->_vm[i].len)
                    {
                        alloc = true;
                        // printfCyan("[copy_out] va: %p is in VMA[%d]: %p-%p\n", va, i, proc->_vma->_vm[i].addr, proc->_vma->_vm[i].addr + proc->_vma->_vm[i].len);
                        break; // 在当前VMA范围内
                    }
                }
            }
            Pte pte = pt.walk(a, 0);
            if (pte.get_data() == 0 && alloc)
            {
                // 如果页表项无效且当前VMA范围内，则分配物理页
                void *mem = k_pmm.alloc_page();
                if (mem == 0)
                {
                    printfRed("[copy_out] alloc page failed for va: %p\n", va);
                    return -1; // 分配失败
                }
                k_pmm.clear_page(mem);
                map_pages(pt, a, PGSIZE, (uint64)mem,
                          PTE_U | PTE_W | PTE_MAT | PTE_D);
            }
            pa = reinterpret_cast<uint64>(pte.pa());
            if (pa == 0)
                return -1;
            n = PGSIZE - (va - a);
            if (n > len)
                n = len;
            pa = to_vir(pa);
            // printfMagenta("copy_out: va: %p, pa: %p, n: %p\n", va, pa, n);
            memmove((void *)((pa + (va - a))), p, n);

            len -= n;
            p = (char *)p + n;
            va = a + PGSIZE;
        }
        return 0;
#endif
    }

    void VirtualMemoryManager::vmunmap(PageTable &pt, uint64 va, uint64 npages, int do_free)
    {
        // printfCyan("vmunmap: va: %p, npages: %d, do_free: %d\n", va, npages, do_free);
        uint64 a;
        Pte pte;

        if ((va % PGSIZE) != 0)
            panic("vmunmap: not aligned");

        for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
        {
            if ((pte = pt.walk(a, 0)).is_null())
                continue;
            // panic("vmunmap: walk");
            if (!pte.is_valid())
                continue;
            ///@brief 这里的逻辑是，如果pte无效，则不需要释放物理页
            /// TODO: 为了mmap的懒分配，所以确实可能出现了惰性页面调用
            // panic("vmunmap: not mapped");
            if (!pte.is_leaf())
                panic("vmunmap: not a leaf");
            if (do_free)
            {
                // printfMagenta("vmunmap: free va: %p, pa: %p\n", a, pte.pa());
                k_pmm.free_page(pte.pa());
            }
            // printfMagenta("vmunmap: unmap va: %p, pa: %p\n", a, pte.pa());
            pte.clear_data();
        }
    }

    PageTable VirtualMemoryManager::vm_create()
    {
        PageTable pt;
        pt.set_global();

        uint64 addr = (uint64)PhysicalMemoryManager::alloc_page();
        if (addr == 0)
            panic("vmm: no mem to crate vm space.");
        k_pmm.clear_page((void *)addr);
        pt.set_base(addr);
        pt.init_ref(); // 初始化引用计数

        return pt;
    }

    int VirtualMemoryManager::vm_copy(PageTable &old_pt, PageTable &new_pt, uint64 start, uint64 size)
    {
        Pte pte;
        uint64 pa, va;
        uint64 va_end;
        uint64 flags;
        void *mem;

        if (!is_page_align(start) || !is_page_align(size))
        {
            panic("uvmcopy: start or size not page aligned");
            return -1;
        }

        va_end = start + size;

        for (va = start; va < va_end; va += PGSIZE)
        {
            if ((pte = old_pt.walk(va, false)).is_null())
            {
                continue;
                panic("uvmcopy: pte should exist for va: %p", va);
            }
            if (pte.is_valid() == 0)
                continue;
            ///@brief 这里的逻辑是，如果pte无效，则不需要释放物理页
            /// TODO: 为了mmap的懒分配，所以确实可能出现了惰性页面调用
            // panic("uvmcopy: page not valid");
            pa = (uint64)pte.pa();
            flags = pte.get_flags();
            if ((mem = mem::PhysicalMemoryManager::alloc_page()) == nullptr)
            {
                vmunmap(new_pt, 0, va / PGSIZE, 1);
                return -1;
            }
            memmove(mem, (const char *)pa, PGSIZE);
            if (map_pages(new_pt, va, PGSIZE, (uint64)mem, flags) == false)
            {
                k_pmm.free_page(mem);
                vmunmap(new_pt, 0, va / PGSIZE, 1);
                return -1;
            }
        }
        return 0;
    }

    void VirtualMemoryManager::vmfree(PageTable &pt, uint64 sz, uint64 base)
    {
        // printfCyan("[vmm] vmfree: free %p bytes\n", sz);
        if (sz > 0)
            vmunmap(pt, base, PGROUNDUP(sz) / PGSIZE, 1);

        // 使用引用计数机制安全释放页表
        // 注意：这里不直接设置pt的_base_addr为0，让dec_ref来处理
        pt.dec_ref();
    }

    void VirtualMemoryManager::uvmclear(PageTable &pt, uint64 va)
    {
        Pte pte = pt.walk(va, 0);
#ifdef RISCV
        if (pte.is_valid())
            pte.set_data(pte.get_data() & ~riscv::PteEnum::pte_user_m);
#elif defined(LOONGARCH)
        if (pte.is_valid())
            pte.set_data(pte.get_data() & ~loongarch::PteEnum::pte_plv_m); // PTE_U
#endif
    }

    uint64 VirtualMemoryManager::uvmalloc(PageTable &pt, uint64 oldsz, uint64 newsz, uint64 flags)
    {
#ifdef RISCV
        uint64 a;
        uint64 pa;

        if (newsz < oldsz) // shrink, not here
            return oldsz;

        a = PGROUNDUP(oldsz); // start from the next page
        // printfBlue("[vmalloc]  another page :%p,walk:%p\n",a,pt.walk(a,0).get_data());
        for (; a < newsz; a += PGSIZE)
        {
            pa = (uint64)k_pmm.alloc_page();
            // printfCyan("[vmalloc] alloc page: %p\n", pa);
            if (pa == 0)
            {
                vmfree(pt, oldsz);
                return 0;
            }
            if (!map_pages(pt, a, PGSIZE, pa, riscv::PteEnum::pte_readable_m | riscv::PteEnum::pte_user_m | flags))
            {
                k_pmm.free_page((void *)pa);
                uvmdealloc(pt, a, oldsz);
                return 0;
            }
        }
        return newsz;
#elif defined(LOONGARCH)
        /// TODO:未测试正确性
        void *mem;
        uint64 a;
        // printfCyan("[vmalloc] oldsz: %p, newsz: %p\n", oldsz, newsz);
        if (newsz < oldsz)
            return oldsz;

        oldsz = PGROUNDUP(oldsz);
        for (a = oldsz; a < newsz; a += PGSIZE)
        {
            mem = k_pmm.alloc_page();
            if (mem == 0)
            {
                // printfCyan("[vmalloc] alloc page failed, oldsz: %p, newsz: %p\n", oldsz, newsz);
                uvmdealloc(pt, a, oldsz);
                return 0;
            }
            memset(mem, 0, PGSIZE);
            if (map_pages(pt, a, PGSIZE, (uint64)mem, flags | PTE_U | PTE_D) == 0)
            {
                // printfCyan("[vmalloc] map page failed, oldsz: %p, newsz: %p\n", oldsz, newsz);
                k_pmm.free_page(mem);
                uvmdealloc(pt, a, oldsz);
                return 0;
            }
        }
        return newsz;
#endif
    }

    uint64 VirtualMemoryManager::uvmdealloc(PageTable &pt, uint64 oldsz, uint64 newsz)
    {
        if (newsz >= oldsz)
            return oldsz;
        if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
            vmunmap(pt,
                    PGROUNDUP(newsz),
                    (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE,
                    1);
        return newsz;
    }
    void VirtualMemoryManager::kvmmap(PageTable &pt, uint64 va, uint64 pa, uint64 sz, uint64 perms)
    {
        if (map_pages(pt, va, sz, pa, perms) == false)
        {
            printf("kvmmap failed\n");
            panic("[vmm] kvmmap failed");
        }
    }

    PageTable VirtualMemoryManager::kvmmake()
    {
        PageTable pt;
        pt.set_global();
        pt.set_base((uint64)k_pmm.alloc_page());
        // pt.init_ref(); // 初始化引用计数
        // printfGreen("[vmm] kvmmake alloc page success\n");
        memset((void *)pt.get_base(), 0, PGSIZE);
        // pt.print_page_table();
#ifdef RISCV
        // uart registers
        kvmmap(pt, UART0, UART0, PGSIZE, PTE_R | PTE_W);
        // printfGreen("[vmm] kvmmake uart0 success\n");
        // uint64 ppp = (uint64)pt.walk_addr(UART0);
        // printfGreen("va: %p, pa: %p\n", UART0, ppp);
        // // virtio mmio disk interface
        kvmmap(pt, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
        // printfGreen("[vmm] kvmmake virtio0 success\n");
        kvmmap(pt, VIRTIO1, VIRTIO1, PGSIZE, PTE_R | PTE_W);
        // printfGreen("[vmm] kvmmake virtio1 success\n");
        // // CLINT
        kvmmap(pt, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
        // printfGreen("[vmm] kvmmake clint success\n");
        // // PLIC
        kvmmap(pt, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
        // printfGreen("[vmm] kvmmake plic success\n");
        // map kernel text executable and read-only.
        kvmmap(pt, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
        // printfGreen("[vmm] kvmmake kernel text success\n");
        // map kernel data and the physical RAM we'll make use of.
        kvmmap(pt, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);
        // printfRed("[vmm] kvmmake kernel data success\n");
        // // map the trampoline for trap entry/exit to
        // // the highest virtual address in the kernel.
        kvmmap(pt, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
        // 我发现trapframe和kstack在xv6里面都没有初始化
        // 因为trampoline的位置在内核和用户页表都一样，
        // 所以他们访问的时候都是通过trampoline进行访问，没有进行映射也没有关系,
        // 所以这里不需要进行映射.
        /*实际上，proc在创建的时候会有两个函数，proc_pagetable,proc_mapstacks,
        这二者会分别映射trampoline和kstack ，我们内核的页表初始化的时候已经映射了trampoline
        这里要映射的*/

        // DEBUG:虚拟化后所有代码卡死，检查所有内核代码映射，KERNBASE到etext
        // printfBlue("etext: %p\n", etext);
        // printfBlue("KERNBASE: %p\n", KERNBASE);
        // for(uint64 va = KERNBASE; va < (uint64)etext; va += PGSIZE)
        // {
        //     uint64 ppp= (uint64)pt.walk_addr(va);
        //     printfRed("va: %p, pa: %p\n", va, ppp);
        // }

        // 初始化堆内存
        kvmmap(pt, vm_kernel_heap_start, HEAP_START, vm_kernel_heap_size, PTE_R | PTE_W);
#elif defined(LOONGARCH)
        kvmmap(pt, ((uint64)etext) & (~(DMWIN_MASK)), (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

#endif
        return pt;
    }

    void VirtualMemoryManager::uvmfirst(PageTable &pt, uint64 src, uint64 sz)
    {
#ifdef RISCV
        // 来自xv6的uvmfirst函数
        char *mem;
        printf("sz: %d\n", sz);
        // if(sz >= PGSIZE)
        //   panic("uvmfirst: more than a page");
        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
        // debug
        // printfYellow("预期映射的pa: %p\n", mem);
        // uint64 pa= (uint64)pt.walk_addr(0);
        // printfYellow("va: %p, pa: %p\n", 0, pa);

        memmove(mem, (void *)src, MIN(sz, PGSIZE));

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, PGSIZE, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
        if (sz > PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + PGSIZE), MIN(sz - PGSIZE, PGSIZE));
        }

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 2 * PGSIZE, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
        if (sz > 2 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 2 * PGSIZE), MIN(sz - 2 * PGSIZE, PGSIZE));
        }

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 3 * PGSIZE, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
        if (sz > 3 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 3 * PGSIZE), MIN(sz - 3 * PGSIZE, PGSIZE));
        }

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 4 * PGSIZE, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
        if (sz > 4 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 4 * PGSIZE), MIN(sz - 4 * PGSIZE, PGSIZE));
        }
#elif defined(LOONGARCH)
        char *mem;
        printf("sz: %d\n", sz);
        // if(sz >= PGSIZE)
        //   panic("uvmfirst: more than a page");
        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 0, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        memmove(mem, (void *)src, MIN(sz, PGSIZE));

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + PGSIZE), MIN(sz - PGSIZE, PGSIZE));
        }

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 2 * PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > 2 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 2 * PGSIZE), MIN(sz - 2 * PGSIZE, PGSIZE));
        }
        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 3 * PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > 3 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 3 * PGSIZE), MIN(sz - 3 * PGSIZE, PGSIZE));
        }
        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 4 * PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > 4 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 4 * PGSIZE), MIN(sz - 4 * PGSIZE, PGSIZE));
        }
        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 5 * PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > 5 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 5 * PGSIZE), MIN(sz - 5 * PGSIZE, PGSIZE));
        }
        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 6 * PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > 6 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 6 * PGSIZE), MIN(sz - 6 * PGSIZE, PGSIZE));
        }

        mem = (char *)k_pmm.alloc_page();
        memset(mem, 0, PGSIZE);
        map_pages(pt, 7 * PGSIZE, PGSIZE, (uint64)mem, PTE_V | PTE_W | PTE_R | PTE_X | PTE_MAT | PTE_PLV | PTE_D | PTE_P);
        if (sz > 7 * PGSIZE)
        {
            memmove(mem, (void *)((uint64)src + 7 * PGSIZE), MIN(sz - 7 * PGSIZE, PGSIZE));
        }
        if (sz > 4 * PGSIZE)
        {
            panic("[vmm] uvmfirst: sz > 4*PGSIZE, this is not supported yet");
        }

#endif
    }

    int VirtualMemoryManager::protectpages(PageTable &pt, uint64 va, uint64 size, int perm)
    {
        uint64 a, last;
        Pte pte;

        // printf("[protectpages] va: %p, size: %p, perm: %p\n", va, size, perm);

        last = PGROUNDDOWN(va + size - 1);

        for (a = PGROUNDDOWN(va); a != last + PGSIZE; a += PGSIZE)
        {
            pte = pt.walk(a, 1);
            if (pte.is_null())
                return -1;
            if (pte.get_data() == 0)
                break;
            if (pte.get_data() & PTE_V)
                pte.set_data(pte.get_data() | perm);
            else
                pte.set_data(pte.get_data() | PTE_U);
        }
        return 0;
    }
}