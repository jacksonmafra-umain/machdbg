// Publishes a global's address and then writes to it forever, so a watchpoint test has a write
// it knows the address of and can wait for. The write is the event under test: the printf
// happens once, up front, and nothing in the loop prints or flushes -- a fixture that wrote to
// stdout in the loop would give a watchpoint on the global plenty of unrelated memory traffic to
// be confused by, and would make a hit hard to attribute.
#include <cstdint>
#include <cstdio>
#include <unistd.h>

// Eight bytes and naturally aligned: x86-64 can only encode watchpoint lengths of 1, 2, 4 and 8,
// and requires the address to be a multiple of the length. A global of any other width would
// make the fixture, rather than the engine, the reason a test could not ask for what it wanted.
volatile uint64_t machbug_watched_value = 0;

int main()
{
    std::printf("%p\n", (void*)&machbug_watched_value);
    std::fflush(stdout);

    for(;;)
    {
        machbug_watched_value = machbug_watched_value + 1;
        usleep(50 * 1000);
    }
    return 0;
}
