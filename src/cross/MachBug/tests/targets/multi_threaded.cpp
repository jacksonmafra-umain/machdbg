// Two threads besides main, each with a name, so a thread-list test has something to tell them
// apart by. std::thread does not name anything; a name only exists because a thread asked for
// one, which is also true of every real program.
#include <pthread.h>
#include <thread>
#include <chrono>
int main()
{
    std::thread a([]{
        pthread_setname_np("worker-a");
        std::this_thread::sleep_for(std::chrono::seconds(30));
    });
    std::thread b([]{
        pthread_setname_np("worker-b");
        std::this_thread::sleep_for(std::chrono::seconds(30));
    });
    a.join();
    b.join();
    return 0;
}
