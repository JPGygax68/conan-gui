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

        void upsert_package2(std::string_view remote, std::string_view name, std::string_view version, std::string_view user, std::string_view channel);

        // TODO: replace with more structured data structure
        auto get_package_list(std::string_view name_filter) -> Package_list;

    private:

        auto query_single_row(const char *query, const char *context = nullptr) -> std::vector<std::string>;

        void exec(const char *statement, std::string_view context = "");
        auto get_row_id(std::string_view table, std::string_view where_clause) -> int64_t;
        auto insert(std::string_view table, std::string_view columns, std::string_view values) -> int64_t;
        void drop_table(std::string_view name);

        sqlite3     *db_handle = nullptr;
    };

} // ns Conan