#pragma once

#include <optional>
#include "./types.h"
#include "./sqlite_wrapper/database.h"


class Cache_db: public SQLite::Database {
public:
    Cache_db();
    ~Cache_db();

    void create_or_update();

    auto get_package_info(int64_t pkg_id) -> std::optional<Package_info>;
    void upsert_package_info(int64_t pkg_id, const Package_info&);

private:
    sqlite3_stmt *      get_pkg_info = nullptr;
    sqlite3_stmt *      upsert_pkg_info = nullptr;
};