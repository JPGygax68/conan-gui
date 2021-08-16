#pragma once

#include <optional>
#include "./types.h"
#include "./sqlite_wrapper/database.h"


struct Package_list_entry: Package_key {
    int64_t id;
};

class Cache_db: public SQLite::Database {
public:
    Cache_db();
    ~Cache_db();

    void create_or_update();

    // TODO: replace with coro generator interface
    void get_list(std::function<bool(SQLite::Row)> row_cb, std::string_view name_filter = "%");
    void upsert_package(std::string_view remote, std::string_view name, std::string_view version, std::string_view user, std::string_view channel);

    auto get_package_info(int64_t pkg_id) -> std::optional<Package_info>;
    void upsert_package_info(int64_t pkg_id, const Package_info&);

    void mark_letter_as_scanned(char letter);

private:
    sqlite3_stmt *      get_list_stmt = nullptr;
    sqlite3_stmt *      get_pkg_info = nullptr;
    sqlite3_stmt *      upsert_pkg_info = nullptr;
    sqlite3_stmt *      upsert_letter_scan_time = nullptr;
};