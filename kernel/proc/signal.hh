#pragma once

#include "types.hh"

namespace proc
{
    class Pcb;
    namespace ipc
    {
        namespace signal
        {
            constexpr int SIGKILL = 9;
            constexpr int SIGSTOP = 19;
            constexpr int SIGQUIT = 3;
            constexpr int SIGRTMAX = 64;

            constexpr int SIG_BLOCK = 0;
            constexpr int SIG_UNBLOCK = 1;
            constexpr int SIG_SETMASK = 2;
            constexpr int SIGCHLD = 17;
            // 简化版 sigset_t，实际你可以用 bitset 或其他方式扩展
            typedef struct
            {
                uint64 sig[1]; // 最多 64 个信号
            } sigset_t;

            // 简化版 sigaction
            typedef struct sigaction
            {
                void (*sa_handler)(int); // 信号处理函数
                sigset_t sa_mask;        // 处理期间阻塞的信号
                int sa_flags;            // 行为标志（未用）
            } sigaction;

            bool is_valid(int sig);

            bool sig_is_member(const uint64 set, int n_sig);

            bool is_ignored(Pcb *now_p, int sig);

            int sigAction(int flag, sigaction *newact, sigaction *oldact);
            int sigprocmask(int how, sigset_t *newset, sigset_t *oldset, size_t sigsize);
            void handle_signal();
            void do_handle(Pcb *p, int signum, sigaction *act);
            void default_handle(Pcb *p, int signum);
            void sig_return();
        } // namespace signal
    } // namespace ipc
} // namespace proc
