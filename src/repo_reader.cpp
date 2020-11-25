#include <algorithm>
#include <cstdio>
#include <iostream>
#include <future>
#include <regex>
#include <cassert>
#include <fmt/core.h>
#include "./database.h"
#include "./repo_reader.h"


namespace Conan {
    
    Repository_reader::Repository_reader(Database& db) : 
        database{ db }
    {
        remotes_ad.obtain([]() {
            auto file_ptr = _popen("conan remote list", "r");
            if (!file_ptr) throw std::system_error(errno, std::generic_category());
            std::vector<std::string> list;
            while (!feof(file_ptr)) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), file_ptr)) {
                    std::string input = buffer;
                    input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                    auto name = input.substr(0, input.find(":"));
                    std::cout << name << std::endl; // TODO: replace with log
                    list.push_back(name);
                }
            }
            fclose(file_ptr);
            return list;
        });
    }
    
    Repository_reader::~Repository_reader()
    {
        stop_reader_thread();
    }

    auto Repository_reader::requeue_all() -> std::future<void>
    {
#ifndef NOT_DEFINED
        return std::async(std::launch::async, [this]() {
            auto& remotes = remotes_ad.get();
            for (auto& remote: remotes)
                queue_repository(remote);
        });
#else
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
#endif
    }

    void Repository_reader::queue_repository(std::string_view repo)
    {
        for (char letter = 'A'; letter <= 'Z'; letter ++) {
            filtered_read(repo, std::string{letter});
            filtered_read(repo, std::string{(char)(letter + 'a' - 'A')});
        }
    }

    void Repository_reader::filtered_read(std::string_view remote, std::string_view name_filter)
    {
        get_package_list(remote, name_filter);
    }

    void Repository_reader::read_letter_all_repositories(char first_letter)
    {
        assert(first_letter >= 'A' && first_letter <= 'Z');

        auto& remotes = remotes_ad.get();
        for (auto& remote: remotes) {
            filtered_read(remote, {&first_letter, 1});
            char letter_lc = first_letter + 'a' - 'A';
            filtered_read(remote, {&letter_lc, 1});
        }
    }

    [[deprecated]]
    void Repository_reader::reader_func()
    {
        while (!terminate) {
            std::unique_lock<std::mutex> lock(task_queue.mutex);
            reader_cv.wait(lock, [this] { return !task_queue.empty(); });
            auto task = task_queue.top();
            lock.unlock();

            std::cout << "Searching remote " << task.repository << ", letter " << task.letter << std::endl;

            get_package_list(task.repository, std::string{task.letter});

            lock.lock();
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

    void Repository_reader::get_package_list(std::string_view remote, std::string_view name_filter) 
    {
        auto file_ptr = _popen(fmt::format("conan search -r {} {}* --raw", remote, name_filter).c_str(), "r");
        if (!file_ptr) throw std::system_error(errno, std::generic_category());

        auto& db = Database::instance();

        auto re = std::regex("([^/]+)/([^@]+)(?:@([^/]+)/(.+))?");

        while (!feof(file_ptr)) {
            char buffer[1024];
            if (fgets(buffer, sizeof(buffer), file_ptr)) {
                std::string input = buffer;
                input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                std::cout << input << std::endl;
                std::smatch m;
                if (std::regex_match(input, m, re)) {
                    std::cout << "Package name: " << m[1] << ", version: " << m[2] << ", user: " << m[3] << ", channel: " << m[4] << std::endl;
                } else {
                    std::cerr << "***FAILED to parse package specifier \"" << input << "\"" << std::endl;
                }
                db.upsert_package(remote, input);
            }
        }

        fclose(file_ptr);
    }

} // Conan
