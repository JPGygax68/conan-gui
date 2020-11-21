#include <algorithm>
#include <cstdio>
#include <fmt/core.h>
#include "./database.h"
#include "./repo_reader.h"


namespace Conan {

    Repository_reader::~Repository_reader()
    {
        stop_reader_thread();
    }

    void Repository_reader::requeue_all()
    {
        // Get list of all repositories
        // TODO: whenn cppcoro becomes available as a Conan package, create a generator 
        auto file_ptr = _popen("conan remote list", "r");
        if (!file_ptr) throw std::system_error(errno, std::generic_category());
        while (!feof(file_ptr)) {
            char buffer[1024];
            if (fgets(buffer, sizeof(buffer), file_ptr)) {
                std::string input = buffer;
                input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                auto name = input.substr(0, input.find(":"));
                queue_repository(name);
            }
        }
        fclose(file_ptr);
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


            // TODO: read, write to database; send a signal ?
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
