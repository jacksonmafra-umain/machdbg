#include <cstdio>

extern "C" __attribute__((noinline, used)) void hit_me()
{
    asm volatile("nop");
}

int main()
{
    std::puts("Hello, ElfBug!");
    hit_me();
    hit_me();
    return 0;
}
