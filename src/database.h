#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <future>
#include <sqlite3.h>


namespace Conan {

    struct Package_info {
        std::string             name;
        std::string             repository;
        // TODO: more ? queries ?
    };

    using Package_list = std::vector<Package_info>;

    class Database {
    public:
        Database();
        ~Database();

        static auto& instance() { static Database db; return db; }

        void upsert_package(std::string_view remote, const std::string& reference);

        // TODO: replace with more structured data structure
        auto get_package_list(std::string_view name_filter) -> Package_list;

    private:
        sqlite3     *db_handle = nullptr;
    };

} // ns Conan