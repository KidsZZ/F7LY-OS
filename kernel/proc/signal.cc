#include "signal.hh"
#include "proc_manager.hh"
#include "physical_memory_manager.hh"

namespace proc
{
    namespace ipc
    {
        namespace signal
        {
            extern "C"
            {
                extern char sig_trampoline[];
#ifdef RISCV
                extern char sig_handler[];
#endif
            }

            int sigAction(int flag, sigaction *newact, sigaction *oldact)
            {
                if (flag <= 0 || flag > signal::SIGRTMAX || flag == signal::SIGKILL || flag == signal::SIGQUIT)
                    return -1;
                proc::Pcb *cur_proc = proc::k_pm.get_cur_pcb();
                if (oldact != nullptr)
                {
                    if (cur_proc->_sigactions[flag])
                        *oldact = *(cur_proc->_sigactions[flag]);
                    else
                        *oldact = {nullptr, {0}, 0};
                }
                if (newact != nullptr)
                {
                    if (!cur_proc->_sigactions[flag])
                    {
                        cur_proc->_sigactions[flag] = new sigaction;
                        if (cur_proc->_sigactions[flag] == nullptr)
                            return -1; // 内存分配失败
                    }
                    *(cur_proc->_sigactions[flag]) = *newact;
                }

                return 0;
            }

            int sigprocmask(int how, sigset_t *newset, sigset_t *oldset, size_t sigsize)
            {
                if (sigsize != sizeof(sigset_t))
                {
                    printfRed("[sigprocmask] sigsize is not sizeof(sigset_t)\n");
                    return -22;
                }

                proc::Pcb *cur_proc = proc::k_pm.get_cur_pcb();
                if (oldset != nullptr)
                    oldset->sig[0] = cur_proc->_sigmask;
                if (newset == nullptr)
                    return 0;

                switch (how)
                {
                case signal::SIG_BLOCK:
                    cur_proc->_sigmask |= newset->sig[0];
                    break;
                case signal::SIG_UNBLOCK:
                    cur_proc->_sigmask &= ~(newset->sig[0]);
                    break;
                case signal::SIG_SETMASK:
                    cur_proc->_sigmask = newset->sig[0];
                    break;
                default:
                    panic("sigprocmask: invalid how value");
                    return -22;
                }

                // 永远不能屏蔽这两个信号
                cur_proc->_sigmask &= ~((1UL << (signal::SIGKILL - 1)) | (1UL << (signal::SIGSTOP - 1)));
                return 0;
            }

            void default_handle(proc::Pcb *p, int signum)
            {
                switch (signum)
                {
                case signal::SIGKILL:
                    p->_killed = true;
                    break;
                case signal::SIGCHLD:
                    panic("[default_handle] SIGCHLD not implemented");
                    break;
                }
            }

            void handle_signal()
            {
                proc::Pcb *p = proc::k_pm.get_cur_pcb();
                if (p->_signal == 0)
                {
                    return; // 没有信号需要处理
                }
                for (uint64 i = 1; i <= proc::ipc::signal::SIGRTMAX && (p->_signal != 0); i++)
                {
                    uint64 sigbit = (1UL << (i - 1));
                    if (!sig_is_member(p->_signal, sigbit))
                    {
                        continue; // 该信号未被设置
                    }
                    int signum = i;
                    if (is_ignored(p, signum))
                    {
                        printf("[handle_signal] Signal %d is ignored sigmask 0x%x\n", signum, p->_sigmask);
                        return;
                    }
                    sigaction *act = p->_sigactions[signum];
                    if (act->sa_handler == nullptr)
                    {
                        printf("[handle_signal] Signal %d has no handler, using default handler\n", signum);
                        default_handle(p, signum);
                    }
                    else
                    {
                        do_handle(p, signum, act);
                    }
                    clear_signal(p, signum);
                }
            }

            void add_signal(proc::Pcb *p, int sig)
            {
                if (sig <= 0 || sig > proc::ipc::signal::SIGRTMAX)
                {
                    panic("[add_signal] Invalid signal number: %d", sig);
                    return;
                }
                // 允许这种情况(所以注释)
                // if (sig_is_member(p->_signal, sig))
                // {
                //     panic("[add_signal] Signal %d is already set", sig);
                //     return;
                // }
                p->_signal |= (1UL << (sig - 1));
            }

            void do_handle(proc::Pcb *p, int signum, sigaction *act)
            {
                if (act == NULL)
                {
                    panic("[do_handle] act is NULL");
                    return;
                }
                if (is_ignored(p, signum))
                {
                    panic("[do_handle] Signal %d is ignored", signum);
                    return;
                }

                signal_frame *frame;
                frame = (signal_frame *)mem::k_pmm.alloc_page();
                frame->mask.sig[0] = p->_sigmask; // 保存当前信号掩码

                if (frame == nullptr)
                {
                    panic("[do_handle] Failed to allocate memory for signal frame");
                    return;
                }
                frame->tf = *(p->_trapframe);
#ifdef RISCV
                p->_trapframe->ra = (uint64)(SIG_TRAMPOLINE + ((uint64)sig_handler - (uint64)sig_trampoline));
#elif LOONGARCH
                panic("sig trampoline not implemented for loongarch");
                p->_trapframe->ra = (uint64)SIG_TRAMPOLINE;
                printf("sig: %p\n", SIG_TRAMPOLINE);
#endif
                p->_trapframe->sp -= PGSIZE;
                p->_trapframe->a0 = signum;
#ifdef RISCV
                p->_trapframe->epc = (uint64)(act->sa_handler);
#elif LOONGARCH
                p->_trapframe->era = (uint64)(act->sa_handler);
#endif
                if (p->sig_frame)
                {
                    frame->next = p->sig_frame;
                }
                else
                {
                    frame->next = nullptr;
                }
                p->sig_frame = frame;
                return;
            }

            void sig_return()
            {
                Pcb *p = proc::k_pm.get_cur_pcb();
                if (p->sig_frame == nullptr)
                {
                    panic("[sig_return] No signal frame to return to");
                    p->_killed = true; // 没有信号帧，直接标记为被kill
                    return;
                }
                signal_frame *frame = p->sig_frame;
                p->_sigmask = frame->mask.sig[0];                        // 恢复信号掩码
                memmove(p->_trapframe, &(frame->tf), sizeof(TrapFrame)); // 恢复陷阱帧
                p->sig_frame = frame->next;                              // 移除当前信号帧
                mem::k_pmm.free_page(frame);                             // 释放信号帧内存
            }

            // tool
            bool is_valid(int sig)
            {
                return (sig <= proc::ipc::signal::SIGRTMAX && sig >= 1);
            }

            bool sig_is_member(const uint64 set, int n_sig)
            {
                return (bool)(1 & (set >> (n_sig - 1)));
            }

            bool is_ignored(Pcb *now_p, int sig)
            {
                return sig_is_member(now_p->_sigmask, sig);
            }

            void clear_signal(Pcb *now_p, int sig)
            {
                if (sig <= 0 || sig > proc::ipc::signal::SIGRTMAX)
                {
                    panic("[clear_signal] Invalid signal number: %d", sig);
                    return;
                }
                if (!sig_is_member(now_p->_signal, sig))
                {
                    panic("[clear_signal] Signal %d is not set", sig);
                    return;
                }
                now_p->_signal &= ~(1UL << (sig - 1));
            }

        } // namespace signal
    } // namespace ipc
} // namespace proc
