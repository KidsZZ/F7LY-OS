- [X] SYS_fork = 1,
- [X] SYS_wait = 3,
- [x] SYS_kill = 6,
- [x] SYS_sleep = 13,
- [ ] SYS_uptime = 14,
- [ ] SYS_mknod = 16,
- [x] SYS_getcwd = 17,
- [ ] SYS_shutdown = 19,
- [x] SYS_dup = 23,
- [x] SYS_dup3 = 24,
- [x] SYS_fcntl = 25,
- [x] SYS_ioctl = 29,
- [x] SYS_mkdirat = 34,
- [x] SYS_unlinkat = 35,
- [ ] SYS_linkat = 37,
- [ ] SYS_umount2 = 39,
- [x] SYS_mount = 40,
- [ ] SYS_faccessat = 48,
- [x] SYS_chdir = 49,
- [ ] SYS_exec = 55,
- [x] SYS_openat = 56,
- [x] SYS_close = 57,
- [x] SYS_pipe2 = 59,
- [ ] SYS_getdents64 = 61,
- [x] SYS_lseek = 62,
- [x] SYS_read = 63,
- [x] SYS_write = 64,
- [ ] SYS_readv = 65,
- [ ] SYS_writev = 66,
- [x] SYS_sendfile = 71,
- [x] SYS_ppoll = 73,
- [x] SYS_readlinkat = 78,
- [ ] SYS_fstatat = 79,
- [x] SYS_fstat = 80,
- [x] SYS_utimensat = 88,
- [x] SYS_exit = 93,
- [x] SYS_exit_group = 94,
- [ ] SYS_set_tid_address = 96,
- [x] SYS_set_robust_list = 99,
- [x] SYS_nanosleep = 101,
- [x] SYS_clock_gettime = 113,
- [ ] SYS_clock_nanosleep = 115,
- [x] SYS_syslog = 116,
- [ ] SYS_sched_yield = 124,
- [ ] SYS_kill_signal = 129,
- [ ] SYS_tkill = 130,
- [ ] SYS_tgkill = 131,
- [x] SYS_rt_sigaction = 134,
- [ ] SYS_rt_sigprocmask = 135,
- [ ] SYS_rt_sigtimedwait = 137,
- [ ] SYS_rt_sigreturn = 139,
- [x] SYS_setgid = 144,
- [x] SYS_setuid = 146,
- [x] SYS_times = 153,
- [x] SYS_uname = 160,
- [x] SYS_gettimeofday = 169,
- [x] SYS_getpid = 172,
- [x] SYS_getppid = 173,
- [x] SYS_getuid = 174,
- [x] SYS_geteuid = 175,
- [x] SYS_getgid = 176,
- [x] SYS_gettid = 178,
- [x] SYS_sysinfo = 179,
- [x] SYS_brk = 214,
- [x] SYS_munmap = 215,
- [ ] SYS_mremap = 216,
- [x] SYS_clone = 220,//TBD
- [ ] SYS_execve = 221,
- [x] SYS_mmap = 222,
- [x] SYS_madvise = 233,
- [x] SYS_wait4 = 260,
- [x] SYS_prlimit64 = 261,
- [ ] SYS_renameat2 = 276,
- [x] SYS_getrandom = 278,
- [x] SYS_statx = 291



优先实现的：
- [x] syscall fork            
- [x] syscall wait            
- [x] syscall chdir           
- [ ] syscall execve          
- [x] syscall brk             
- [ ] syscall set_tid_address 
- [ ] syscall set_robust_list 
- [ ] syscall prlimit64       
- [ ] syscall readlinkat      
- [ ] syscall getrandom       
- [ ] syscall clock_gettime         
- [ ] syscall mprotect        
- [ ] syscall getuid          
- [ ] syscall statx           
- [ ] syscall getgid          
- [ ] syscall setgid          
- [ ] syscall setuid          
- [ ] syscall getpid          
- [ ] syscall sigaction       
- [ ] syscall getppid         
- [ ] syscall uname           
- [x] syscall getcwd          
- [x] syscall ioctl           
- [ ] syscall openat          
- [ ] syscall fcntl           
- [ ] syscall getpgid         
- [ ] syscall setpgid         
- [ ] syscall geteuid         
- [ ] syscall ppoll           
- [ X] syscall read


yield似乎是对的，sche_yield需要调用yield，后成功。
sleep已成功，问题在于进入了nanosleep，nanosleep的参数获取有问题，测试只有两个参数，我们获取了四个参数导致错误。

path错误的问题已修复，增加一个判断条件。

brk问题在于vmalloc的for循环，     a = PGROUNDUP(oldsz); // start from the next page  
        for (; a < newsz; a += PGSIZE)