#include <string>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <concepts>
#include <regex>
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

namespace SQLite {

    static void throw_sqlite_error(int category, const char *err_msg = nullptr, std::string_view context = "") {
        using namespace std::string_literals;
        auto msg = fmt::format("SQLite error (category: {0})", sqlite3_errstr(category));
        if (err_msg) msg += ": "s + err_msg;
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

        if (version == 12) {
            exec(R"(
                BEGIN TRANSACTION;
                CREATE TABLE packages2_backup AS SELECT id, remote, name, version, user, channel, last_poll FROM packages2;
                DROP TABLE packages2;
                CREATE TABLE packages2 (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    remote STRING NOT NULL,
                    name STRING NOT NULL,
                    version STRING NOT NULL,
                    user STRING,
                    channel STRING,
                    last_poll DATETIME
                );
                INSERT INTO packages2 SELECT * FROM packages2_backup;
                DROP TABLE packages2_backup;
                COMMIT;
            )", "trying to remove SEMVER fields from \"packages2\"");
        }
        else {
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
        }
        if (version <= 5 || version == 12)
            exec("create unique index if not exists packages2_unique on packages2(remote, name, version, user, channel)");

        if (version == 9) {
            exec(R"(
                alter table packages add column ver_major INTEGER;
                alter table packages add column ver_minor INTEGER;
                alter table packages add column ver_patch INTEGER;
            )", "trying to add SEMVER columns to table \"packages2\"");
        }

        if (version == 7 || version == 8) {
            drop_table("pkg_info");
        }
        if (version == 13) {
            exec(R"(
                ALTER TABLE pkg_info ADD COLUMN author STRING;
                ALTER TABLE pkg_info ADD COLUMN topics STRING;
            )", "trying to add new fields to table \"pkg_info\"");
        }
        else {
            exec(R"(
                create table if not exists pkg_info (
                    pkg_id INTEGER,
                    recipe_id,
                    remote STRING,
                    url STRING,
                    license STRING,
                    description STRING,
                    provides STRING,
                    author STRING,
                    topics STRING,
                    creation_date DATETIME,
                    last_poll DATETIME,
                    FOREIGN KEY (pkg_id) REFERENCES packages2(id)
                )
            )", "trying to create pkg_info table");
        }
        if (version == 8) {
            db_err = sqlite3_exec(db_handle, R"(
                create unique index if not exists pkg_info_pkg_id on pkg_info (pkg_id);
            )", nullptr, nullptr, &errmsg);
            if (db_err != 0) throw_sqlite_error(db_err, "trying to create index pkg_info_pkg_id on pkg_info table");
        }

        if (version == 10 || version == 11) {
            select("select id, version from packages2", [&](int col_count, const char * const col_values[], const char * const col_names[]) -> int {
                static const auto re = std::regex("^(\\d+)\\.(\\d+)\\.(\\d+)$");
                std::smatch m;
                std::string value = col_values[1];
                if (std::regex_match(value, m, re)) {
                    auto major = std::stoi(m[1]), minor = std::stoi(m[2]), patch = std::stoi(m[3]);
                    std::cout << "SEMVER: " << major << "." << minor << "." << patch << std::endl;
                    // TODO: very poor example, use prepared statements and parameter binding!
                    // TODO: could computed fields be used instead ?
                    exec(fmt::format("update packages2 set ver_major={1}, ver_minor={2}, ver_patch={3} where id={0}", col_values[0], major, minor, patch).c_str(),
                        "trying to set SEMVER values in \"packages2\""
                    );
                }
                return 0;
            });
        }

        sqlite3_create_function(
            db_handle,
            "SEMVER_PART", 2,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_DIRECTONLY,
            nullptr,
            [](sqlite3_context* context, int argc, sqlite3_value** argv) {
                // TODO: improve to support a fourth part (after either a dash or another dot)
                static const auto re = std::regex("^(\\d+)\\.(\\d+)\\.(\\d+)$");
                // auto& re = *static_cast<std::regex*>(sqlite3_user_data(context));
                if (argc == 2) {
                    std::smatch m;
                    std::string version{ (const char*)sqlite3_value_text(argv[0]) };
                    int index = sqlite3_value_int(argv[1]);
                    if (!(index >= 1 && index <= 3)) {
                        sqlite3_result_error(context, "The second parameter to SEMVER_PART() must be between 1 and 3", -1);
                        return;
                    }
                    if (std::regex_match(version, m, re)) {
                        sqlite3_result_int(context, std::stoi(m[index]));
                        return;
                    }
                }
                sqlite3_result_null(context);
            },
            nullptr, nullptr
        );

        exec("pragma user_version = 14;", "trying to set database version");

        stmt_upsert_package_description = [&]() {
            static const auto statement = R"(
                insert into pkg_info
                    (pkg_id, description, license, provides, author, topics, last_poll)
                    values(:PKG_ID, :DESCRIPTION, :LICENSE, :PROVIDES, :AUTHOR, :TOPICS, datetime('now'))
                on conflict (pkg_id) do update
                    set pkg_id=:PKG_ID, description=:DESCRIPTION, license=:LICENSE, author=:AUTHOR, topics=:TOPICS, last_poll=datetime('now');
            )";
            sqlite3_stmt *stmt = nullptr;
            auto err = sqlite3_prepare_v2(db_handle, statement, -1, &stmt, nullptr);
            if (err != SQLITE_OK) throw_sqlite_error(err, nullptr, "trying to prepare statement upsert_package_description");
            return stmt;
        }();


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

    // void Database::set_package_info(sqlite3_int64 pkg_id, const Package_info& info)
    // {
    //     // TODO (:PKG_ID, : DESCRIPTION, : LICENSE, : PROVIDES, : AUTHOR, : TOPICS, datetime('now'))
    //     auto stmt = stmt_upsert_package_description;
    //     int err = 0;
    //     err = sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, "PKG_ID"     ), pkg_id);
    //     err = sqlite3_bind_text (stmt, sqlite3_bind_parameter_index(stmt, "DESCRIPTION"), info.description.data(), info.description.size(), nullptr);
    //     err = sqlite3_bind_text (stmt, sqlite3_bind_parameter_index(stmt, "LICENSE"    ), info.license    .data(), info.license    .size(), nullptr);
    //     err = sqlite3_bind_text (stmt, sqlite3_bind_parameter_index(stmt, "PROVIDES"   ), info.provides   .data(), info.provides   .size(), nullptr);
    //     err = sqlite3_bind_text (stmt, sqlite3_bind_parameter_index(stmt, "AUTHOR"     ), info.author     .data(), info.author     .size(), nullptr);
    //     err = sqlite3_bind_text (stmt, sqlite3_bind_parameter_index(stmt, "TOPICS"     ), info.topics     .data(), info.topics     .size(), nullptr);
    //     err = sqlite3_step(stmt);
    //     if (err != SQLITE_DONE) throw_sqlite_error(err, nullptr, "trying to execute prepared statement upsert_package_info");
    // }

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

    auto Database::select(
        std::initializer_list<std::string_view> columns,
        std::string_view table,
        std::string_view where_clause,
        std::string_view order_by_clause,
        std::initializer_list<std::string_view> params
    ) -> Rows
    {
        using namespace std::string_literals;

        auto statement = fmt::format("SELECT {0} FROM {1}", join_strings(columns), table);
        if (!where_clause.empty()) statement += fmt::format(" WHERE {0}", where_clause);
        if (!order_by_clause.empty()) statement += fmt::format(" ORDER BY {0}", order_by_clause);

        // Prepare the statement
        sqlite3_stmt* stmt = nullptr;
        auto err = sqlite3_prepare_v2(db_handle, statement.c_str(), -1, &stmt, nullptr);
        if (err != SQLITE_OK) throw_sqlite_error(err, nullptr, "trying to prepare statement: "s + statement);

        // Bind the parameters
        for (auto i = 0U; auto& param: params) {
            err = sqlite3_bind_text(stmt, i, param.data(), param.size(), nullptr);
            if (err != SQLITE_OK) throw_sqlite_error(err, nullptr, "trying to bind value to prepared statement");
            i ++;
        }

        // Execute the statement and collect the rows
        Rows rows;
        if (err != SQLITE_DONE) throw_sqlite_error(err, nullptr, "trying to execute prepared statement upsert_package_info");
        do {
            err  = sqlite3_step(stmt);
            if (err == SQLITE_ROW) {
                Row row;
                for (auto i = 0U; const auto col_name: columns) {
                    auto type = sqlite3_column_type(stmt, i);
                    if (type == SQLITE_INTEGER)
                        row.emplace_back(Value{ sqlite3_column_int64(stmt, i) });
                    else if (type == SQLITE_FLOAT)
                        row.emplace_back(Value{ sqlite3_column_double(stmt, i) });
                    else if (type == SQLITE_TEXT)
                        row.emplace_back(Value{ (const char *)sqlite3_column_text(stmt, i) });
                    else
                        row.emplace_back(Value{});
                }
                rows.push_back(row);
            }
        } while (err != SQLITE_ERROR && err != SQLITE_DONE && err != SQLITE_MISUSE);

        return rows;
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

    void Database::upsert(
        std::string_view table,
        std::initializer_list<std::string_view> unique_columns,
        std::initializer_list<std::string_view> extra_columns,
        std::initializer_list<Value> values
    ) {
        using namespace std::string_literals;

        std::vector<std::string> all_columns, unique_equals;
        for (auto i = 0U; auto& col: unique_columns) {
            unique_equals.push_back(fmt::format("{0}=?{1}", col, ++i));
            all_columns.push_back(std::string{col});
        }
        for (auto& col: extra_columns)
            all_columns.push_back(std::string{col});

        std::vector<std::string> all_placeholders, extra_placeholders, extra_setters;
        for (auto i = 0U; auto& col: unique_columns) all_placeholders.push_back(fmt::format("?{0}", ++i));
        for (auto i = unique_columns.size(); auto& col: extra_columns) {
            ++i;
            auto placeholder = fmt::format("?{0}", i);
            all_placeholders.push_back(placeholder);
            extra_placeholders.push_back(placeholder);
            extra_setters.push_back(fmt::format("{0}=?{1}", col, i));
        }

        auto statement = fmt::format("INSERT INTO {0} ({1}) VALUES({2}) ON CONFLICT({3}) DO UPDATE SET ({4})",
            /* 0 */ table,
            /* 1 */ join_strings(all_columns, ", "),
            /* 2 */ join_strings(all_placeholders, ", "),
            /* 3 */ join_strings(unique_columns, ", "),
            /* 4 */ join_strings(extra_setters, ", ")
        );

        // Prepare the statement
        sqlite3_stmt* stmt = nullptr;
        auto err = sqlite3_prepare_v2(db_handle, statement.c_str(), -1, &stmt, nullptr);
        if (err != SQLITE_OK) throw_sqlite_error(err, nullptr, "trying to prepare statement: "s + statement);

        // Bind the parameters
        for (auto i = 1U; auto & param: values) {
            int err = 0;
            if      (param.index() == 0) err = sqlite3_bind_null(stmt, i);
            else if (param.index() == 1) err = sqlite3_bind_int64(stmt, i, std::get<1>(param));
            else if (param.index() == 2) err = sqlite3_bind_double(stmt, i, std::get<2>(param));
            else if (param.index() == 3) err = sqlite3_bind_text(stmt, i, std::get<3>(param).data(), std::get<3>(param).size(), nullptr);
            ++i;
            assert(err >= 0);
        }

        err = sqlite3_step(stmt);
        if (err != SQLITE_DONE) throw_sqlite_error(err, nullptr, "trying to execute prepared statement: "s + statement);
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
