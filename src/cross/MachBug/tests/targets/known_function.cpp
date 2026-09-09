// Publishes the address of a function it then calls forever, so a breakpoint test knows an
// address that is real code, is reached repeatedly, and did not have to be inferred from a
// register.
#include <cstdint>
#include <cstdio>
#include <unistd.h>

// noinline, and not static: the address published below has to be the address actually called,
// and an inlined or folded function has neither.
__attribute__((noinline)) void machbug_breakpoint_target(void)
{
    // A volatile write, so the body cannot be optimised away to nothing and the function keeps a
    // real instruction to trap on.
    static volatile uint64_t counter = 0;
    counter = counter + 1;
}

int main()
{
    std::printf("%p\n", (void*)&machbug_breakpoint_target);
    std::fflush(stdout);
    for(;;)
    {
        machbug_breakpoint_target();
        usleep(50 * 1000);
    }
    return 0;
}
