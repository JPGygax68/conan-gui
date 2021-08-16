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

        void update_package_list(std::string_view remote, std::string_view name_filter);

        // SQLite::Database&           database;

        async_data<std::vector<std::string>> remotes_ad;

        std::thread                 reader_thread;
        std::condition_variable     reader_cv;
        bool                        term_flag = false;
    };

} // ns Conan