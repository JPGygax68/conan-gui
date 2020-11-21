#include <algorithm>
#include <cstdio>
#include <iostream>
#include <future>
#include <fmt/core.h>
#include "./database.h"
#include "./repo_reader.h"


namespace Conan {

    Repository_reader::~Repository_reader()
    {
        stop_reader_thread();
    }

    auto Repository_reader::requeue_all() -> std::future<void>
    {
        return std::async(std::launch::async, [&]() {
            auto file_ptr = _popen("conan remote list", "r");
            if (!file_ptr) throw std::system_error(errno, std::generic_category());
            while (!feof(file_ptr)) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), file_ptr)) {
                    std::string input = buffer;
                    input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                    auto name = input.substr(0, input.find(":"));
                    std::cout << name << std::endl;
                    queue_repository(name);
                }
            }
            fclose(file_ptr);
        });
    }

    void Repository_reader::queue_repository(std::string_view repo)
    {
        for (char letter = 'A'; letter <= 'Z'; letter ++) {
            queue_single_task(repo, letter);
            queue_single_task(repo, letter + 'a' - 'A');
        }
    }
    
    void Repository_reader::queue_single_task(std::string_view repo, char letter)
    {
        task_queue.add_or_requeue(Task{ std::string{repo}, letter });
        start_reader_thread_if_not_running();
    }

    void Repository_reader::reader_func()
    {
        while (!terminate) {
            std::unique_lock<std::mutex> lk(task_queue.mutex);
            reader_cv.wait(lk, [this] { return !task_queue.empty(); });

            auto& task = task_queue.top();

            std::cout << "Searching remote " << task.repository << ", letter " << task.letter << std::endl;

            auto file_ptr = _popen(fmt::format("conan search -r {} {}* --raw", task.repository, task.letter).c_str(), "r");
            if (!file_ptr) throw std::system_error(errno, std::generic_category());

            while (!feof(file_ptr)) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), file_ptr)) {
                    std::string input = buffer;
                    input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                    //std::cout << input << std::endl;
                    auto split_pos = input.find("/");
                    auto name = input.substr(0, split_pos);
                    auto specifier = input.substr(split_pos + 1);
                    // TODO: insert into database (with notification to database clients!)
                    std::cout << name << ": " << specifier << std::endl;
                    database.upsert_package(task.repository, input);
                }
            }

            task_queue.pop();
        }
    }

    void Repository_reader::start_reader_thread_if_not_running()
    {
        if (!reader_thread.joinable()) {
            terminate = false;
            reader_thread = std::thread([this]() { reader_func(); });
        }
    }

    void Repository_reader::stop_reader_thread()
    {
        if (reader_thread.joinable()) {
            terminate = true;
            reader_cv.notify_one();
            reader_thread.join();
        }
    }

    void Repository_reader::get_package_list_internal(std::string_view remote, char first_letter) 
    {
        auto file_ptr = _popen(fmt::format("conan search -r {} {}* --raw", remote, first_letter).c_str(), "r");
        if (!file_ptr) throw std::system_error(errno, std::generic_category());

        auto& db = Database::instance();

        while (!feof(file_ptr)) {
            char buffer[1024];
            if (fgets(buffer, sizeof(buffer), file_ptr)) {
                std::string input = buffer;
                input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                //std::cout << input << std::endl;
                // auto split_pos = input.find("/");
                // auto name = input.substr(0, split_pos);
                // auto specifier = input.substr(split_pos + 1);
                db.upsert_package(remote, input);
            }
        }

        fclose(file_ptr);
    }

    void Repository_reader::get_package_list(std::string_view remote, char first_letter) 
    {
        get_package_list_internal(remote, first_letter);

        if (first_letter >= 'A' && first_letter <= 'Z') {
            get_package_list_internal(remote, first_letter + 'a' - 'A');
        }
    }


} // Conan
