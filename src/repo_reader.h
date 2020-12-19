#pragma once

#include <string_view>
#include <queue>
#include <mutex>
#include <future>
#include "./async_data.h"
#include "./types.h"
#include "./sqlite_wrapper/database.h"


namespace Conan {

    class Repository_reader {
    public:
        
        explicit Repository_reader(); // SQLite::Database& db);

        ~Repository_reader();

        [[deprecated]]
        auto requeue_all() -> std::future<void>;

        [[deprecated]]
        void queue_repository(std::string_view repo);

        void filtered_read(std::string_view repo, std::string_view name_filter);
        void read_letter_all_repositories(char first_letter);

        // auto get_info(
        //     std::string_view remote, 
        //     std::string_view package, 
        //     std::string_view user, 
        //     std::string_view channel, 
        //     std::string_view version
        // ) -> Package_info;

        auto get_info(const Package_key&) -> Package_info;

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

        void update_package_list(std::string_view remote, std::string_view name_filter);

        // SQLite::Database&           database;

        async_data<std::vector<std::string>> remotes_ad;

        std::thread                 reader_thread;
        Task_queue                  task_queue;
        std::condition_variable     reader_cv;
        bool                        terminate = false;
    };

} // ns Conan