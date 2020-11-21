#include <string>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fmt/core.h>

#include "./database.h"


namespace Conan {
    
    static void throw_sqlite_error(int err, const char *context) {
        using namespace std::string_literals;
        auto error_text = sqlite3_errstr(err);
        std::cerr << error_text << "; context: " << context << std::endl;
        throw std::runtime_error("SQLite error: "s + sqlite3_errstr(err) + " (" + std::to_string(err) + "); context: " + context);
    }

    Database::Database()
    {
        std::filesystem::path appdata_dir;
        
#ifdef WIN32
        appdata_dir = getenv("LOCALAPPDATA");
        auto db_dir = appdata_dir / "ConanDB";
#else
        // TODO: TEST!
        appdata_dir = getenv("HOME");
        if (appdata_dir.empty()) {
            auto passwd = getpwuid(getuid());
            if (!passwd) throw std::runtime_error("Impossible to determine user home directory");
            appdata_dir = passwd->pw_dir;
        }
        auto db_dir = appdata_dir / ".ConanDB";
#endif

        std::filesystem::create_directories(db_dir);

        auto db_path = db_dir / "conan.sqlite";

        char* errmsg;
        int db_err = 0;

        db_err = sqlite3_open(db_path.string().c_str(), &db_handle);
        if (db_err != SQLITE_OK) throw_sqlite_error(db_err, "trying to open/create database");

        db_err = sqlite3_exec(db_handle, R"(
            create table if not exists packages (
                remote STRING,
                reference STRING,
                last_poll DATETIME
            );
        )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to create packages table");

        db_err = sqlite3_exec(db_handle, R"(
            create unique index if not exists remote_reference on packages (remote, reference);
        )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to create index remote_reference on packages table");
    }

    Database::~Database()
    {
        if (db_handle != nullptr) 
            sqlite3_close(db_handle);
    }

    void Database::upsert_package(std::string_view remote, const std::string& reference)
    {
        char* errmsg;
        int db_err;

        auto statement = fmt::format(R"(
            insert into packages (remote, reference, last_poll) 
            values('{0}', '{1}', datetime('now'))
            on conflict (remote, reference) do update set last_poll=datetime('now');
        )", remote, reference);
        db_err = sqlite3_exec(db_handle, statement.c_str(), nullptr, nullptr, &errmsg);
        
        if (db_err != 0) throw_sqlite_error(db_err, "trying to insert/upsert into packages table");
    }

    auto Database::get_package_list() -> std::vector<std::string>
    {
        std::vector<std::string> list;

        auto db_err = sqlite3_exec(db_handle, R"(
            select * from packages;
        )", [](void *list_, int coln, char *textv[], char *namev[]) -> int {
            auto plst = static_cast<decltype(list)*>(list_);            
            plst->push_back(std::string{textv[0]} + ":" + textv[1]);
            return 0;
        }, &list, nullptr);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to query packages");

        return list;
    }

} // ns Conans
