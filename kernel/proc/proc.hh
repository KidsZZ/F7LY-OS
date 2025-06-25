#pragma once
#include "types.hh"
#ifdef RISCV
#include "mem/riscv/pagetable.hh"
#elif defined(LOONGARCH)
#include "mem/loongarch/pagetable.hh"
#endif
#include "trapframe.hh"
#include "context.hh"
#include "spinlock.hh"
#include <EASTL/string.h>
#include "signal.hh"
#include "prlimit.hh"
#include "futex.hh"
#include "fs/vfs/file/file.hh"
namespace fs
{
    class dentry;
    class file;
} // namespace fs
namespace proc
{
    constexpr int NVMA = 10; // 每个进程最多的虚拟内存区域数量
    enum ProcState
    {
        UNUSED,
        USED,
        SLEEPING,
        RUNNABLE,
        RUNNING,
        ZOMBIE
    };

    constexpr uint num_process = 32;      // 系统中允许的最大进程数量
    constexpr int default_proc_prio = 10; // 默认进程优先级
    constexpr int lowest_proc_prio = 19;  // 最低进程优先级
    constexpr int highest_proc_prio = 0;  // 最高进程优先级
    constexpr uint max_open_files = 128;  // 每个进程最多可以打开的文件数量

    struct program_section_desc
    {
        void *_sec_start = nullptr; // virtual address
        ulong _sec_size = 0;
        const char *_debug_name = nullptr;
    };
    constexpr int max_program_section_num = 16;
    constexpr int max_vma_num = 10;
    class Pcb
    {
        // friend ProcessManager;
        // friend ShmManager;
        // friend Scheduler;

    public:
        SpinLock _lock;         // 进程控制块的锁，用于并发访问控制
        int _gid = num_process; // 全局ID，用于在进程池中唯一标识进程

        // 文件系统相关
        fs::dentry *_cwd; // current working directory
        eastl::string _cwd_name;
        fs::file *_ofile[max_open_files]; // 进程打开的文件列表 (文件描述符 -> 文件结构)
        bool _fl_cloexec[max_open_files]; // 记录每个文件描述符的 close-on-exec 标志
        eastl::string exe;                // absolute path of the executable file

        // 进程状态信息
        enum ProcState _state; // 进程当前状态 (unused, used, sleeping, runnable, running, zombie)
        void *_chan;           // 进程睡眠时等待的通道 (例如：某个锁或事件)
        int _killed;           // 进程是否被标记为kill (非零表示被kill)
        int _xstate;           // 进程退出状态，用于父进程wait()获取
        int _pid;              // 进程ID (Process ID)
        Pcb *_parent;          // 父进程的PCB指针

        char _name[30]; // 进程名称 (用于调试)

        // 内存管理相关
        uint64 _kstack = 0;    // 内核栈的虚拟地址
        uint64 _sz;            // 进程用户空间的内存大小 (字节)
        mem::PageTable _pt;    // 用户空间的页表
        TrapFrame *_trapframe; // 保存用户态 TrapFrame 的地址 (用于系统调用和异常处理)

        // 上下文切换
        Context _context; // 保存进程的上下文信息 (寄存器等)，用于进程切换

        // 调度相关
        int _slot;     // 分配给进程的时间片剩余量
        int _priority; // 进程优先级 (0最高，19最低)

        // TODO:共享内存相关
        // uint _shm;             // 共享内存的起始虚拟地址
        // void *_shmva[SHM_NUM]; // 指向共享内存区域的虚拟地址数组
        // uint _shmkeymask;      // 用于标记哪些共享物理内存页被该进程使用

        // 消息队列相关
        uint _mqmask; // 用于标记进程使用的消息队列

        // 运行时间统计
        uint64 _start_tick;     // 进程开始运行时的时钟节拍数
        uint64 _user_ticks;     // 进程在用户态运行的时钟节拍总数
        uint64 _last_user_tick; // 进程上次返回用户态时的时钟节拍数

        uint64 _hp; // 临时堆指针 (注释说明后续会删除)

        // 虚拟内存区域 (VMA) - 注释中提出了疑问，这里保留但需要进一步理解其用途
        vma _vm[NVMA]; // 虚拟内存区域数组

        // 线程/futex 相关
        void *_futex_addr; // Used for futex
        int *_set_child_tid = nullptr;
        int *_clear_child_tid = nullptr;
		int wakeup2addtimes = 0; // 记录额外的唤醒次数, 比如两个线程都退出了,但是父进程还没有调用waitpid, 需要额外唤醒一次

        robust_list_head *_robust_list = nullptr;

        // for prlimit 进程资源相关
        rlimit64 _rlim_vec[ResourceLimitId::RLIM_NLIMITS];

        // signal处理相关
        proc::ipc::signal::sigaction *_sigactions[proc::ipc::signal::SIGRTMAX + 1];
        uint64 sigmask;
        int _signal; // 等待的信号
        // 程序段相关
        TODO("TBF")
        program_section_desc _prog_sections[max_program_section_num];
        int _prog_section_cnt = 0;

        // 用于set_tid_address
        uint64 clear_child_tid;

    public:
        Pcb();
        void init(const char *lock_name, uint gid);
        void map_kstack(mem::PageTable &pt);
        fs::dentry *get_cwd() { return _cwd; }
        int get_priority();

        void print_context();

    public:
        Context *get_context() { return &_context; }

    public:
        // fs::Dentry *get_cwd() { return _cwd; }
        void kill()
        {
            _lock.acquire();
            _killed = 1;
            _lock.release();
        }
        Pcb *get_parent() { return _parent; }
        void set_state(ProcState state) { _state = state; }
        void set_xstate(int xstate) { _xstate = xstate; }
        size_t get_sz() { return _sz; }
        // void set_chan(void *chan) { _chan = chan; }
        uint get_pid() { return _pid; }
        uint get_ppid() { return _parent ? _parent->_pid : 0; }
        TrapFrame *get_trapframe() { return _trapframe; }
        uint64 get_kstack() { return _kstack; }
        mem::PageTable *get_pagetable() { return &_pt; }
        ProcState get_state() { return _state; }
        char *get_name() { return _name; }
        uint64 get_size() { return _sz; }
        uint64 get_last_user_tick() { return _last_user_tick; }
        uint64 get_user_ticks() { return _user_ticks; }
        fs::file *get_open_file(int fd)
        {
            if (fd < 0 || fd >= (int)max_open_files)
                return nullptr;
            return _ofile[fd];
        }

        void set_trapframe(TrapFrame *tf) { _trapframe = tf; }

        void set_last_user_tick(uint64 tick) { _last_user_tick = tick; }
        void set_user_ticks(uint64 ticks) { _user_ticks = ticks; }

        bool is_killed()
        {
            int k;
            _lock.acquire();
            k = _killed;
            _lock.release();
            return k;
        }
    };

    extern Pcb k_proc_pool[num_process]; // 全局进程池
    void print_context1(Context *ctx);   // 打印进程上下文
}
