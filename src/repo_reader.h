#pragma once

#include <string_view>
#include <queue>
#include <mutex>
#include <future>
#include "./database.h"


namespace Conan {

    class Repository_reader {
    public:
        
        explicit Repository_reader(Database& db): database{db} {}

        ~Repository_reader();

        auto requeue_all() -> std::future<void>;

        void queue_repository(std::string_view repo);

        void queue_single_task(std::string_view repo, char letter);

    private:
        struct Task {
            std::string     repository;
            char            letter;
            bool operator > (const Task& other) const {
                if (repository < other.repository) return false;
                if (repository > other.repository) return true;
                if (letter < other.letter) return false;
                return true;
            }
            bool operator == (const Task& other) const = default;
        };

        class Task_queue : public std::priority_queue<Task, std::vector<Task>, std::greater<Task>> {
        public:
            void add_or_requeue(const Task& task) {
                std::lock_guard<std::mutex> lock{ mutex };
                _remove(task);
                push(task);
            }
            bool remove(const Task& task) {
                std::lock_guard<std::mutex> lock{ mutex };
                return _remove(task);
            }
            bool _remove(const Task& task) {
                auto it = std::find(c.begin(), c.end(), task);
                if (it != c.end()) {
                    c.erase(it);
                    std::make_heap(c.begin(), c.end(), this->comp);
                    return true;
                }
                return false;
            }
            std::mutex                  mutex;
        };

        void start_reader_thread_if_not_running();
        void stop_reader_thread();
        void reader_func();

        void get_package_list_internal(std::string_view remote, char first_letter);
        void get_package_list(std::string_view remote, char first_letter);

        Database&                   database;

        std::thread                 reader_thread;
        Task_queue                  task_queue;
        std::condition_variable     reader_cv;
        bool                        terminate = false;
    };

} // ns Conan