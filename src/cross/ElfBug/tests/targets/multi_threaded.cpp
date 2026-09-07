#include <pthread.h>
#include <atomic>
#include <vector>

static std::atomic<int> gRanCount{0};

static void* worker(void*)
{
    gRanCount.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

int main()
{
    std::vector<pthread_t> threads(5);
    for(auto & t : threads) pthread_create(&t, nullptr, worker, nullptr);
    for(const auto & t : threads) pthread_join(t, nullptr);
    return gRanCount.load(std::memory_order_relaxed);
}
