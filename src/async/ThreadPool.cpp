#include "async/ThreadPool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] { return stop_.load() || !jobs_.empty(); });
                    if (stop_.load() && jobs_.empty()) return;
                    job = std::move(jobs_.front());
                    jobs_.pop();
                }
                try {
                    job();
                } catch (const std::exception& e) {
                    std::cerr << "[ThreadPool] Job threw exception: " << e.what() << "\n";
                } catch (...) {
                    std::cerr << "[ThreadPool] Job threw unknown exception\n";
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true);
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void ThreadPool::enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
}

size_t ThreadPool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}
