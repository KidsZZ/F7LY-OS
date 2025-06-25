#include "proc.hh"
#include "context.hh"
#include "virtual_memory_manager.hh"
#include "physical_memory_manager.hh"
namespace proc
{
    Pcb k_proc_pool[num_process]; // 全局进程池

    Pcb::Pcb()
    {
        _state = UNUSED;
        _chan = nullptr;
        _killed = 0;
        _xstate = 0;
        _pid = 0;
        _parent = nullptr;
        _sz = 0;
        _trapframe = nullptr;
        _kstack = 0;
        _slot = 0;
        _priority = default_proc_prio;
    }

    void Pcb::init(const char *lock_name, uint gid)
    {
        _lock.init(lock_name);
        _state = ProcState::UNUSED;
        _gid = gid;
        _kstack = mem::VirtualMemoryManager::kstack_vm_from_gid(_gid);
        // TODO
        for (auto &of : _ofile)
            of = nullptr;
        for (auto &cloexec : _fl_cloexec)
            cloexec = false;
        //TODO: 资源限制
        _rlim_vec[ResourceLimitId::RLIMIT_STACK].rlim_cur = 0;
        _rlim_vec[ResourceLimitId::RLIMIT_STACK].rlim_max = 0;


        // 初始化信号处理函数指针
        for (int i = 0; i <= ipc::signal::SIGRTMAX; ++i) {
            _sigactions[i] = nullptr;
        }
        sigmask = 0;
    }

    void Pcb::map_kstack(mem::PageTable &pt)
    {

        if (_kstack == 0)
            panic("pcb was not init");

        char *pa = (char *)mem::k_pmm.alloc_page();
        if (pa == 0)
            panic("pcb map kstack: no memory");
        mem::k_pmm.clear_page((void *)pa);
#ifdef RISCV
        // printfBlue("map kstack: %p, end: %p\n", _kstack, _kstack + PGSIZE-1);
        if (!mem::k_vmm.map_pages(pt, _kstack, PGSIZE, (uint64)pa,
                                  riscv::PteEnum::pte_readable_m |
                                      riscv::PteEnum::pte_writable_m))
            panic("kernel vm map failed");
#elif defined(LOONGARCH)
        /// TODO:未测试正确性
        /// copy from 华科
        if (!mem::k_vmm.map_pages(pt, _kstack, PGSIZE, (uint64)pa,
                                  PTE_NX | PTE_P | PTE_W | PTE_MAT | PTE_D | PTE_PLV))
            panic("kernel vm map failed");
#endif
    }

    int Pcb::get_priority()
    {
        _lock.acquire();
        int priority = _priority;
        _lock.release();
        return priority;
    }

    void Pcb::print_context()
    {
        printf("Context register values for process %d (%s):\n", this->_pid, this->_name);
        print_context1(this->get_context());
    }

    void print_context1(Context *ctx)
    {
        printf(" ra  = 0x%x\n", ctx->ra);
        printf(" sp  = 0x%x\n", ctx->sp);
        printf(" s0  = 0x%x\n", ctx->s0);
        printf(" s1  = 0x%x\n", ctx->s1);
        printf(" s2  = 0x%x\n", ctx->s2);
        printf(" s3  = 0x%x\n", ctx->s3);
        printf(" s4  = 0x%x\n", ctx->s4);
        printf(" s5  = 0x%x\n", ctx->s5);
        printf(" s6  = 0x%x\n", ctx->s6);
        printf(" s7  = 0x%x\n", ctx->s7);
        printf(" s8  = 0x%x\n", ctx->s8);
        #ifdef RISCV
        printf(" s9  = 0x%x\n", ctx->s9);
        printf(" s10 = 0x%x\n", ctx->s10);
        printf(" s11 = 0x%x\n", ctx->s11);
        #endif
    }
}
