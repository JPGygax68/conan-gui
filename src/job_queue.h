#pragma once

#include <functional>
#include <thread>
#include <mutex>


class Job_queue {
public:
    using Job = std::function<void(void)>;

    static auto& instance() { 
        static Job_queue _instance; return _instance; 
    }

    ~Job_queue() {
        term_flag = true;
        if (worker.joinable()) {
            cond_var.notify_one();
            worker.join();
        }
    }

    void queue_job(Job&& job) {
        auto lock = std::unique_lock{mutex};
        jobs.push(std::move(job));
        lock.unlock();
        if (!worker.joinable()) 
            worker = std::thread{[this]() { execute_jobs(); }};
        cond_var.notify_one();
    }

private:

    void execute_jobs() {

        for (Job job; (job = std::move(get_next_job()));) {
            job();
        }
    }

    auto get_next_job() -> Job {
        
        auto lock = std::unique_lock{ mutex };
        cond_var.wait(lock, [this]() { 
            return term_flag || !jobs.empty(); });
        if (term_flag) {
            lock.unlock();
            return Job{};        
        }
        // auto&& job = std::move(jobs.front());
        auto job = jobs.front();
        jobs.pop();
        lock.unlock();
        return job;
    }

    std::mutex              mutex;
    std::queue<Job>         jobs;
    std::thread             worker;
    std::condition_variable cond_var;
    bool                    term_flag = false;
};