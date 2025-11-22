#include "threadpool.h"

ThreadPool::ThreadPool(size_t n) : running(true) {
    for (size_t i = 0; i < n; ++i) {
        workers.emplace_back([this]{
            this->worker();
        });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(Task t) {
    {
        std::unique_lock<std::mutex> lock(mu);
        tasks.push(std::move(t));
    }
    cv.notify_one();
}

void ThreadPool::stop() {
    if (!running.exchange(false)) return;

    cv.notify_all();

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }
}

void ThreadPool::worker() {
    while (running) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(mu);
            cv.wait(lock, [&] {
                return !tasks.empty() || !running;
            });

            if (!running && tasks.empty())
                return;

            task = std::move(tasks.front());
            tasks.pop();
        }

        try {
            task();
        } catch (...) {
            // ignore exceptions inside tasks
        }
    }
}
