#include <cassert>
#include <string>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <concepts>
#include <format>

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
            auto msg = std::format("SQLite error {0}: {1}", sqlite3_errstr(code), sqlite3_errmsg(db));
            // if (err_msg) msg += ": "s + err_msg;
            if (!context.empty()) { msg += "; context: "; msg += context; }
            return msg;
        }
    };


    Database::Database(const char *filename)
    {
        using namespace std::filesystem;

        std::filesystem::create_directories(path{filename}.parent_path());

        auto db_err = sqlite3_open_v2(filename, &db_handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if (db_err != SQLITE_OK) throw sqlite_error(db_handle, db_err, "trying to open/create database");
    }

    Database::~Database()
    {
        if (db_handle != nullptr)
            sqlite3_close(db_handle);
    }

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

        auto statement = std::format("SELECT {0} FROM {1}", join_strings(columns), table);
        if (!where_clause.empty()) statement += std::format(" WHERE {0}", where_clause);
        if (!order_by_clause.empty()) statement += std::format(" ORDER BY {0}", order_by_clause);

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
        auto statement = std::format("select rowid from {0} where {1}", table, where_clause);
        auto fields = query_single_row( statement.c_str(), std::format("trying to get rowid of table \"{0}\"", table).c_str());
        return fields.empty() ? 0 : std::stoi(fields[0]);
    }

    auto Database::insert(std::string_view table, std::string_view columns, std::string_view values) -> int64_t
    {
        char* errmsg;
        int db_err;

        auto statement = std::format(R"(
            insert into {0} ({1}) values({2})
        )", table, columns, values);
        db_err = sqlite3_exec(db_handle, statement.c_str(), nullptr, nullptr, &errmsg);

        if (db_err != 0) throw sqlite_error(db_handle, db_err, std::format("trying to insert into table \"{0}\"", table));

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
            unique_equals.push_back(std::format("{0}=?{1}", col, ++i));
            all_columns.push_back(std::string{ col });
        }
        for (auto& col : extra_columns)
            all_columns.push_back(std::string{ col });

        std::vector<std::string> all_placeholders, extra_placeholders, extra_setters;
        for (auto i = 0U; auto & col: unique_columns) all_placeholders.push_back(std::format("?{0}", ++i));
        for (auto i = unique_columns.size(); auto & col: extra_columns) {
            ++i;
            auto placeholder = std::format("?{0}", i);
            all_placeholders.push_back(placeholder);
            extra_placeholders.push_back(placeholder);
            extra_setters.push_back(std::format("{0}=?{1}", col, i));
        }

        auto statement = std::format("INSERT INTO {0} ({1}) VALUES({2}) ON CONFLICT({3}) DO UPDATE SET {4};",
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
            if (sqlite3_column_type(stmt, i) == SQLITE_NULL)
                row.push_back(Value{nullptr});
            else {
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
        }
        return row;
    }

    void Database::drop_table(std::string_view version)
    {
        exec(std::format("drop table if exists {0};", version).c_str(), std::format("trying to drop table \"{0}\"", version));
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
