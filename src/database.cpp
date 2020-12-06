#include <string>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <concepts>
#include <fmt/core.h>

#include "./database.h"


std::string replace_string(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

namespace Conan {
    
    static void throw_sqlite_error(int category, const char *err_msg, std::string_view context = "") {
        using namespace std::string_literals;
        auto msg = fmt::format("SQLite error (category: {0}): {1}", sqlite3_errstr(category), err_msg);
        if (!context.empty()) { msg += "; context: "; msg += context; }
        std::cerr << msg << std::endl;
        throw std::runtime_error(msg);
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

        unsigned version = 0;
        db_err = sqlite3_exec(db_handle, "PRAGMA user_version",
            [](void* version_, int coln, char* textv[], char* namev[]) -> int {
            auto pver = static_cast<decltype(version)*>(version_);
            *pver = std::stoi(textv[0]);
            return 0;
        }, &version, nullptr);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to retrieve user_version");

        db_err = sqlite3_exec(db_handle, R"(
            create table if not exists packages (
                remote STRING,
                reference STRING,
                last_poll DATETIME
            );
        )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to create packages table");
        
        if (version == 0) {
            db_err = sqlite3_exec(db_handle, R"(
                drop table if exists packages_old;
                create table packages_old as select * from packages;
                drop table packages;
                create table packages as select * from packages_old;
                pragma user_version = 1;
            )", nullptr, nullptr, &errmsg);
            if (db_err != 0) throw_sqlite_error(db_err, "trying to add primary key to packages table");
        }

        db_err = sqlite3_exec(db_handle, R"(
            create unique index if not exists remote_reference on packages (remote, reference);
        )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to create index remote_reference on packages table");

        db_err = sqlite3_exec(db_handle, R"(
            create table if not exists package_info (
                remote STRING,
                reference STRING,
                last_poll DATETIME
            );
        )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, "trying to create packages table");

        exec(R"(
            create table if not exists packages2 (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                remote STRING NOT NULL,
                name STRING NOT NULL,
                version STRING NOT NULL,
                user STRING,
                channel STRING,
                last_poll DATETIME
            )
        )", "trying to create packages2 table");
        if (version <= 5) 
            exec("create unique index if not exists packages2_unique on packages2(remote, name, version, user, channel)");

        if (version == 7 || version == 8) {
            drop_table("pkg_info");
        }
        exec(R"(
            create table if not exists pkg_info (
                pkg_id INTEGER, 
                recipe_id,
                remote STRING,
                url STRING,
                license STRING,
                description STRING,
                provides STRING,
                creation_date DATETIME,
                last_poll DATETIME,
                FOREIGN KEY (pkg_id) REFERENCES packages2(id)
            )
        )", "trying to create pkg_info table");
        if (version == 8) {
            db_err = sqlite3_exec(db_handle, R"(
                create unique index if not exists pkg_info_pkg_id on pkg_info (pkg_id);
            )", nullptr, nullptr, &errmsg);
            if (db_err != 0) throw_sqlite_error(db_err, "trying to create index pkg_info_pkg_id on pkg_info table");
        }

        exec("pragma user_version = 9;", "trying to set database version");
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

    void Database::upsert_package2(std::string_view remote, std::string_view name, std::string_view version, std::string_view user, std::string_view channel)
    {
        auto statement = fmt::format(R"(
            insert into packages2 (remote, name, version, user, channel, last_poll) 
                values('{0}', '{1}', '{2}', '{3}', '{4}', datetime('now'))
            on conflict (remote, name, version, user, channel) do update set last_poll=datetime('now');
        )", remote, name, version, user, channel);

        exec(statement.c_str(), "trying to upsert into package2");
    }

    auto Database::get_package_designators(std::string_view name_filter) -> Package_designators
    {
        Package_designators list;
        char *errmsg;

        auto db_err = sqlite3_exec(db_handle, fmt::format(R"(
            select distinct name, remote from packages2 where name like '{0}%';
        )", name_filter).c_str(), [](void *list_, int coln, char *textv[], char *namev[]) -> int {
            auto plst = static_cast<decltype(list)*>(list_);            
            plst->push_back({ .name = textv[0], .repository = textv[1] });
            return 0;
        }, &list, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, errmsg, "trying to query packages");

        return list;
    }

    // TODO: replace with setter that sets all important fields
    void Database::set_package_description(std::string_view id, std::string_view description)
    {
        auto statement = fmt::format(R"(
            insert into pkg_info (pkg_id, description, last_poll) values('{0}', '{1}', datetime('now'))
            on conflict (pkg_id) do update set pkg_id='{0}', description='{1}', last_poll=datetime('now');
        )", id, escape_single_quotes(description));

        exec(statement.c_str(), "trying to upsert package_description into pkg_info");
    }

    auto Database::query_single_row(const char *query, const char *context) -> std::vector<std::string>
    {
        char *errmsg;
        int dberr;

        std::vector<std::string> output;

        dberr = sqlite3_exec(db_handle, query, [](void* out_, int coln, char* textv[], char* namev[]) -> int {
            auto& pout = *static_cast<decltype(output)*>(out_);
            for (auto i = 0; i < coln; i ++) pout.push_back(textv[i]);
            return 0;
        }, &output, &errmsg);

        if (dberr != 0) throw_sqlite_error(dberr, context);

        return output;
    }

    void Database::exec(const char *statement, std::string_view context)
    {
        char* errmsg;
        int db_err;

        db_err = sqlite3_exec(db_handle, statement, nullptr, nullptr, &errmsg);

        if (db_err != 0) throw_sqlite_error(db_err, errmsg, context);
    }

    void Database::select(const char *statement, select_callback cb)
    {
        char* errmsg;
        auto db_err = sqlite3_exec(db_handle, statement, [](void* data, int coln, char* textv[], char* namev[]) -> int {
            select_callback& cb = *static_cast<select_callback*>(data);
            return cb(coln, (const char * const *)textv, (const char * const *)namev);
        }, &cb, &errmsg);
        if (db_err != 0) throw_sqlite_error(db_err, errmsg);
    }

    auto Database::get_row_id(std::string_view table, std::string_view where_clause) -> int64_t
    {
        auto statement = fmt::format("select rowid from {0} where {1}", table, where_clause);
        auto fields = query_single_row( statement.c_str(), fmt::format("trying to get rowid of table \"{0}\"", table).c_str());
        return fields.empty() ? 0 : std::stoi(fields[0]);
    }

    auto Database::insert(std::string_view table, std::string_view columns, std::string_view values) -> int64_t
    {
        char* errmsg;
        int db_err;

        auto statement = fmt::format(R"(
            insert into {0} ({1}) values({2})
        )", table, columns, values);
        db_err = sqlite3_exec(db_handle, statement.c_str(), nullptr, nullptr, &errmsg);

        if (db_err != 0) throw_sqlite_error(db_err, errmsg, fmt::format("trying to insert into table \"{0}\"", table));

        return sqlite3_last_insert_rowid(db_handle);
    }

    void Database::drop_table(std::string_view name)
    {
        exec(fmt::format("drop table if exists {0}", name).c_str(), fmt::format("trying to drop table \"{0}\"", name));
    }

    auto Database::escape_single_quotes(std::string_view s) -> std::string
    {
        std::string result;
        for (auto ch: s) {
            if (ch == '\'') result += "''"; else result += ch;
        }
        return result;
    }

} // ns Conans
