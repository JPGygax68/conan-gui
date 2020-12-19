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

namespace SQLite {

    class sqlite_error: public std::exception {
    public:
        explicit sqlite_error(sqlite3* db, int code, /* const char* err_msg = nullptr, */ std::string_view context = ""):
            exception(make_message(db, code, /* err_msg, */ context).c_str())
        {}
    private:
        static auto make_message(sqlite3* db, int code, /* const char* err_msg, */ std::string_view context) -> std::string {
            using namespace std::string_literals;
            auto msg = fmt::format("SQLite error {0}: {1}", sqlite3_errstr(code), sqlite3_errmsg(db));
            // if (err_msg) msg += ": "s + err_msg;
            if (!context.empty()) { msg += "; context: "; msg += context; }
            return msg;
        }
    };


    Database::Database(const char *filename)
    {
        using namespace std::filesystem;

        std::filesystem::create_directories(path{filename}.parent_path());

        auto db_err = sqlite3_open(filename, &db_handle);
        if (db_err != SQLITE_OK) throw sqlite_error(db_handle, db_err, "trying to open/create database");
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

        if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to insert/upsert into packages table");
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
        if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to query packages");

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
    //     if (err != SQLITE_DONE) throw sqlite_error(err, nullptr, "trying to execute prepared statement upsert_package_info");
    // }

    auto Database::query_single_row(const char *list_query, const char *context) -> std::vector<std::string>
    {
        char *errmsg;
        int dberr;

        std::vector<std::string> output;

        dberr = sqlite3_exec(db_handle, list_query, [](void* out_, int coln, char* textv[], char* namev[]) -> int {
            auto& pout = *static_cast<decltype(output)*>(out_);
            for (auto i = 0; i < coln; i ++) pout.push_back(textv[i]);
            return 0;
        }, &output, &errmsg);

        if (dberr != 0) throw sqlite_error(db_handle, dberr, context);

        return output;
    }

    void Database::exec(const char *statement, std::string_view context)
    {
        char* errmsg;
        int db_err;

        db_err = sqlite3_exec(db_handle, statement, nullptr, nullptr, &errmsg);

        if (db_err != 0) throw sqlite_error(db_handle, db_err, context);
    }

    void Database::select(const char *statement, select_callback cb)
    {
        char* errmsg;
        auto db_err = sqlite3_exec(db_handle, statement, [](void* data, int coln, char* textv[], char* namev[]) -> int {
            select_callback& cb = *static_cast<select_callback*>(data);
            return cb(coln, (const char * const *)textv, (const char * const *)namev);
        }, &cb, &errmsg);
        if (db_err != 0) throw sqlite_error(db_handle, db_err, errmsg);
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
        if (err != SQLITE_OK) throw sqlite_error(db_handle, err, "trying to prepare statement: "s + statement);

        // Bind the parameters
        for (auto i = 0U; auto& param: params) {
            err = sqlite3_bind_text(stmt, i, param.data(), param.size(), nullptr);
            if (err != SQLITE_OK) throw sqlite_error(db_handle, err, "trying to bind value to prepared statement");
            i ++;
        }

        // Execute the statement and collect the rows
        Rows rows;
        if (err != SQLITE_DONE) throw sqlite_error(db_handle, err, "trying to execute prepared statement upsert_package_info");
        do {
            err  = sqlite3_step(stmt);
            if (err == SQLITE_ROW) {
                Row row;
                for (auto i = 0U; const auto col_name: columns) {
                    auto type = sqlite3_column_decltype(stmt, i);
                    if ("INTEGER"s == type) // type if (type == SQLITE_INTEGER)
                        row.emplace_back(Value{ sqlite3_column_int64(stmt, i) });
                    else if ("FLOAT"s == type) // if (type == SQLITE_FLOAT)
                        row.emplace_back(Value{ sqlite3_column_double(stmt, i) });
                    else if ("TEXT"s == type) // if (type == SQLITE_TEXT)
                        row.emplace_back(Value{ (const char *)sqlite3_column_text(stmt, i) });
                    else
                        row.emplace_back(Value{});
                }
                rows.push_back(row);
            }
        } while (err != SQLITE_ERROR && err != SQLITE_DONE && err != SQLITE_MISUSE);

        return rows;
    }

    auto Database::select_one(std::string_view statement, const std::initializer_list<Value> keys) -> Row
    {
        auto stmt = prepare_statement(statement);
        if (!execute(stmt, keys))
            throw std::runtime_error("expected statement to return at least one row but it returned none");
        auto row = get_row(stmt);
        sqlite3_reset(stmt);
        return row;
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

        if (db_err != 0) throw sqlite_error(db_handle, db_err, fmt::format("trying to insert into table \"{0}\"", table));

        return sqlite3_last_insert_rowid(db_handle);
    }

    auto Database::prepare_upsert(
        std::string_view table,
        std::initializer_list<std::string_view> unique_columns,
        std::initializer_list<std::string_view> extra_columns
    ) -> sqlite3_stmt*
    {
        using namespace std::string_literals;

        std::vector<std::string> all_columns, unique_equals;
        for (auto i = 0U; auto & col: unique_columns) {
            unique_equals.push_back(fmt::format("{0}=?{1}", col, ++i));
            all_columns.push_back(std::string{ col });
        }
        for (auto& col : extra_columns)
            all_columns.push_back(std::string{ col });

        std::vector<std::string> all_placeholders, extra_placeholders, extra_setters;
        for (auto i = 0U; auto & col: unique_columns) all_placeholders.push_back(fmt::format("?{0}", ++i));
        for (auto i = unique_columns.size(); auto & col: extra_columns) {
            ++i;
            auto placeholder = fmt::format("?{0}", i);
            all_placeholders.push_back(placeholder);
            extra_placeholders.push_back(placeholder);
            extra_setters.push_back(fmt::format("{0}=?{1}", col, i));
        }

        auto statement = fmt::format("INSERT INTO {0} ({1}) VALUES({2}) ON CONFLICT({3}) DO UPDATE SET {4}",
            /* 0 */ table,
            /* 1 */ join_strings(all_columns, ", "),
            /* 2 */ join_strings(all_placeholders, ", "),
            /* 3 */ join_strings(unique_columns, ", "),
            /* 4 */ join_strings(extra_setters, ", ")
        );

        return prepare_statement(statement);
    }

    auto Database::prepare_statement(std::string_view statement) -> sqlite3_stmt*
    {
        sqlite3_stmt* stmt = nullptr;
        auto err = sqlite3_prepare_v2(db_handle, statement.data(), statement.size(), &stmt, nullptr);
        if (err != SQLITE_OK) throw sqlite_error(db_handle, err, "trying to prepare statement");
        return stmt;
    }

    bool Database::execute(sqlite3_stmt* stmt, std::initializer_list<Value> values)
    {
        if (sqlite3_stmt_busy(stmt) == 0) {
            // Bind the parameters
            for (auto i = 1U; auto & param: values) {
                int err = 0;
                if      (param.index() == 0) err = sqlite3_bind_null  (stmt, i);
                else if (param.index() == 1) err = sqlite3_bind_int64 (stmt, i, std::get<1>(param));
                else if (param.index() == 2) err = sqlite3_bind_double(stmt, i, std::get<2>(param));
                else if (param.index() == 3) err = sqlite3_bind_text  (stmt, i, std::get<3>(param).data(), std::get<3>(param).size(), nullptr);
                ++i;
                assert(err >= 0);
            }
        }

        int code = sqlite3_step(stmt);
        if      (code == SQLITE_ROW)  return true;
        else if (code == SQLITE_DONE) return false;
        else 
            throw sqlite_error(db_handle, code, "trying to step through prepared statement");
    }

    void Database::execute(std::string_view statement, std::string_view context)
    {
        int db_err;

        db_err = sqlite3_exec(db_handle, std::string{statement}.c_str(), nullptr, nullptr, nullptr);

        if (db_err != 0) throw sqlite_error(db_handle, db_err, context);
    }

    auto Database::get_row(sqlite3_stmt* stmt) -> Row
    {
        using namespace std::string_literals;

        Row row;
        for (auto i = 0U; i < sqlite3_column_count(stmt); i++) {
            auto type = sqlite3_column_decltype(stmt, i);
            if (type == nullptr || "INTEGER"s == type)
                row.push_back(Value{sqlite3_column_int64 (stmt, i)});
            else if ("FLOAT"s == type) 
                row.push_back(Value{sqlite3_column_double(stmt, i)});
            else if ("BLOB"s == type) {
                auto data = (const uint8_t*)sqlite3_column_blob(stmt, i);
                auto size = sqlite3_column_bytes(stmt, i);
                row.push_back(std::move(Blob{data, data + size}));
            }
            else if ("STRING"s == type) {
                auto text = (const char*)sqlite3_column_text(stmt, i);
                row.push_back(Value{text ? text : ""});
            }
            else if ("NULL"s == type)
                row.push_back(Value{nullptr});
            else if ("DATETIME"s == type) {
                auto text = (const char*)sqlite3_column_text(stmt, i);
                row.push_back(Value{ text ? text : "" });
            }
            else 
                assert(false);
        }
        return row;
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
