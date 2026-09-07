#pragma once

#include "bridgemain.h"
#include "concurrentqueue/blockingconcurrentqueue.h"
#include <functional>

class JobQueue
{
    moodycamel::BlockingConcurrentQueue<std::function<bool()>> mQueue;
    std::thread mThread;

public:
    JobQueue()
    {
        start();
    }

    ~JobQueue()
    {
        stop();
    }

    JobQueue(const JobQueue &) = delete;
    JobQueue & operator=(const JobQueue &) = delete;
    JobQueue(JobQueue &&) = delete;
    JobQueue & operator=(JobQueue &&) = delete;

    bool on_worker_thread() const
    {
        return std::this_thread::get_id() == mThread.get_id();
    }

    void start()
    {
        if(mThread.joinable())
        {
            return;
        }

        mThread = std::thread([this]()
        {
            while(true)
            {
                std::function<bool()> job;
                mQueue.wait_dequeue(job);
                if(!job())
                {
                    break;
                }
            }
        });
    }

    void stop()
    {
        if(!mThread.joinable())
        {
            return;
        }

        mQueue.enqueue([]()
        {
            return false;
        });
        mThread.join();
    }

    bool async(std::function<void()> job, bool always_queue = true)
    {
        // Directly call the job if we're on the worker thread and we are not forced to queue.
        if(!always_queue && on_worker_thread())
        {
            job();
            return true;
        }

        return mQueue.enqueue([job = std::move(job)]()
        {
            job();
            return true;
        });
    }

    // Non-void return
    template < typename F,
               typename R = decltype(std::declval<F>()()),
               typename std::enable_if < !std::is_void<R>::value, int >::type = 0 >
    R await(F && job)
    {
        R result = {};
        impl_await([job = std::forward<F>(job), &result]()
        {
            result = job();
        });
        return result;
    }

    // Void return
    template <typename F,
              typename R = decltype(std::declval<F>()()),
              typename std::enable_if<std::is_void<R>::value, int>::type = 0>
    void await(F && job)
    {
        impl_await(std::forward<F>(job));
    }

private:
    template<class Func>
    void impl_await(Func && job)
    {
        // Calling await from another job would deadlock, so we execute it directly instead.
        if(on_worker_thread())
        {
            job();
            return;
        }

        auto event = CreateEventW(nullptr, false, false, nullptr);
        auto func = [job = std::forward<Func>(job), event]()
        {
            job();
            SetEvent(event);
            return true;
        };
        if(!mQueue.enqueue(std::move(func)))
        {
            CloseHandle(event);
            return;
        }
        while(WaitForSingleObject(event, 100) == WAIT_TIMEOUT)
        {
            //workaround for scripts being executed on the GUI thread
            GuiProcessEvents();
        }
        CloseHandle(event);
    }
};
