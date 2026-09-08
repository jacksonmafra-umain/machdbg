#include <thread>
#include <chrono>
int main()
{
    std::thread a([]{ std::this_thread::sleep_for(std::chrono::seconds(30)); });
    std::thread b([]{ std::this_thread::sleep_for(std::chrono::seconds(30)); });
    a.join();
    b.join();
    return 0;
}
