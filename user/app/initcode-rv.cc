#include "user.hh"

extern "C"
{
    __attribute__((section(".text.startup")))
    int main()
    {
        start_shell();
        // basic_test();
        // busybox_musl_test();
        // libc_musl_test();
        // libcbench_test();
        // iozone_test();
        shutdown();
        return 0;
    }
}