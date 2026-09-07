#include <cstdint>

int main()
{
    volatile int* p = reinterpret_cast<int*>(static_cast<std::uintptr_t>(0));
    *p = 42;
    return 0;
}
