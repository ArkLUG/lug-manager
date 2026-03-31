#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>
#include <vector>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 4);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> job);
    size_t pending() const;

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> jobs_;
    mutable std::mutex                mutex_;
    std::condition_variable           cv_;
    std::atomic<bool>                 stop_{false};
};
