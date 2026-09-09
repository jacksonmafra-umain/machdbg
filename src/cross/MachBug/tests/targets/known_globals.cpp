// Publishes the address and value of a global, then parks, so a memory test can read and write a
// cell whose address and expected contents it knows -- rather than an address inferred from a
// register, which proves less and breaks differently.
#include <cstdint>
#include <cstdio>
#include <unistd.h>

volatile uint64_t machbug_known_value = 0x0123456789ABCDEFull;

int main()
{
    std::printf("%p %llx\n", (void*)&machbug_known_value,
                (unsigned long long)machbug_known_value);
    std::fflush(stdout);
    for(;;)
        sleep(1);
    return 0;
}
