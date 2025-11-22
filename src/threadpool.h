#pragma once
#include <vector>
#include <thread>
#include <functional>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>

class ThreadPool {
public:
    using Task = std::function<void()>;

    ThreadPool(size_t n);
    ~ThreadPool();

    void enqueue(Task t);
    void stop();

private:
    void worker();   // <-- MISSING earlier; must be here

    std::vector<std::thread> workers;
    std::queue<Task> tasks;

    std::mutex mu;
    std::condition_variable cv;
    std::atomic<bool> running{true};
};
