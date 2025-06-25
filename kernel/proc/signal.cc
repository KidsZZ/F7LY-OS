#include "signal.hh"
#include "proc_manager.hh"

namespace proc
{
    namespace ipc
    {
        namespace signal
        {

            int sigAction(int flag, sigaction *newact, sigaction *oldact)
            {
                if(flag == signal::SIGCHLD){
                    printfRed("[sigAction] SIGCHLD is inited\n");
                }
                if (flag <= 0 || flag >= signal::SIGRTMAX || flag == signal::SIGKILL || flag == signal::SIGQUIT)
                    return -1;
                proc::Pcb *cur_proc = proc::k_pm.get_cur_pcb();
                if (oldact != nullptr)
                {
                    if (cur_proc->  _sigactions[flag])
                        *oldact = *(cur_proc->  _sigactions[flag]);
                    else
                        *oldact = {nullptr, {0}, 0};
                }
                if (newact != nullptr)
                {
                    if (!cur_proc->  _sigactions[flag])
                    {
                        cur_proc->  _sigactions[flag] = new sigaction;
                        if (cur_proc->  _sigactions[flag] == nullptr)
                            return -1; // 内存分配失败
                    }
                    *(cur_proc->  _sigactions[flag]) = *newact;
                }

                return 0;
            }

            int sigprocmask(int how, sigset_t *newset, sigset_t *oldset, size_t sigsize)
            {
                if (sigsize != sizeof(sigset_t)){
                    printfRed("[sigprocmask] sigsize is not sizeof(sigset_t)\n");
                    return -22;
                }

                proc::Pcb *cur_proc = proc::k_pm.get_cur_pcb();
                if (oldset != nullptr)
                    oldset->sig[0] = cur_proc->sigmask;
                if (newset == nullptr)
                    return 0;

                if(sig_is_member(newset->sig[0], signal::SIGCHLD)){
                    if(how == signal::SIG_BLOCK){
                        printfRed("[sigprocmask] SIGCHLD is blocked\n");
                    } else if(how == signal::SIG_UNBLOCK){
                        printfRed("[sigprocmask] SIGCHLD is unblocked\n");
                    }
                }

                if(how == signal::SIG_SETMASK){
                    int sigsetmask = (1 & (newset->sig[0] >> (signal::SIGCHLD - 1)));
                    printfRed("[sigprocmask] SIGCHLD is %s\n", sigsetmask ? "blocked" : "unblocked");
                }

                switch (how)
                {
                case signal::SIG_BLOCK:
                    cur_proc->sigmask |= newset->sig[0];
                    break;
                case signal::SIG_UNBLOCK:
                    cur_proc->sigmask &= ~(newset->sig[0]);
                    break;
                case signal::SIG_SETMASK:
                    cur_proc->sigmask = newset->sig[0];
                    break;
                default:
                    panic("sigprocmask: invalid how value");
                    return -22;
                }

                // 永远不能屏蔽这两个信号
                TODO("sigmask should not block SIGKILL and SIGSTOP");
                // cur_proc->sigmask &= ~((1UL << (signal::SIGKILL - 1)) | (1UL << (signal::SIGSTOP - 1)));
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
                    // 在此处进行 SIGCHLD 的默认处理
                    printfRed("[default_handle] Default handling SIGCHLD for process %d\n", p->_pid);
					// 回收所有僵尸子进程
					while (k_pm.wait4(-1, 0, 0) > 0) {
						// 继续回收直到没有更多僵尸子进程
					}
                    break;
                }
            }

            void handle_signal()
            {
                proc::Pcb *p = proc::k_pm.get_cur_pcb();
                if(p->_signal == 0){
                    return; // 没有信号需要处理
                }
                int signum = p->_signal;
                if(!is_valid(signum)){
                    panic("[handle_signal] Invalid signal number");
                }
                if(is_ignored(p, signum)){
                    printf("[handle_signal] Signal %d is ignored sigmask 0x%x\n", signum, p->sigmask);
                    return;
                }
                [[maybe_unused]]sigaction *act = p->_sigactions[signum];
                // if(act->sa_handler == nullptr){
                //     printf("[handle_signal] Signal %d has no handler, using default handler\n", signum);
                //     default_handle(p, signum);
                //     p->_signal = 0;
                // } else {
                //     do_handle(p, signum, act);
                //     p->_signal = 0;
                // }
                default_handle(p, signum);
                p->_signal = 0; // 清除信号
            }

            void do_handle(proc::Pcb *p, int signum, sigaction *act)
            {
                panic("todo: do_handle not implemented");
            }

            void sig_return()
            {
                panic("todo: sig_return not implemented");
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
                return sig_is_member(now_p->sigmask, sig);
            }
        } // namespace signal
    } // namespace ipc
} // namespace proc
