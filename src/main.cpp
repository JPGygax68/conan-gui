#include <iostream>
#include <cstdio>
#include <future>
#include "./database.h"
#include "./repo_reader.h"


int main(int argc, char *argv[])
{
    Conan::Database database;
    Conan::Repository_reader repo_reader{database};

    // Get list of all repositories
    auto future = std::async(std::launch::async, [&]() {
        auto file_ptr = _popen("conan remote list", "r");
        if (!file_ptr) throw std::system_error(errno, std::generic_category());
        while (!feof(file_ptr)) {
            char buffer[1024];
            if (fgets(buffer, sizeof(buffer), file_ptr)) {
                std::string input = buffer;
                input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                auto name = input.substr(0, input.find(":"));
                std::cout << name << std::endl;
                repo_reader.queue_repository(name);
            }
        }
        fclose(file_ptr);
    });

    future.get(); // wait till the remotes are queued
}