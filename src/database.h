#pragma once

#include <string_view>
#include <sqlite3.h>


namespace Conan {

    class Database {
    public:
        Database();
        ~Database();

        static auto& instance() { static Database db; return db; }

        void upsert_package(std::string_view remote, const std::string& reference);

    private:
        sqlite3     *db_handle = nullptr;
    };

} // ns Conan