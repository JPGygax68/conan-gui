#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <future>
#include <variant>
#include <sqlite3.h>


template<typename T>
concept StringView = std::is_same<T, std::string_view>::value;

namespace SQLite {

    using Blob = std::vector<uint8_t>;

    //struct Value: public std::variant<
    //    nullptr_t,
    //    int64_t,
    //    double,
    //    std::string,    // TODO: use unsigned char string as SQLite does ?
    //    Blob
    //> {
    //    auto& as_int64 () { return std::get<1>(*this); }
    //    auto& as_double() { return std::get<2>(*this); }
    //    auto& as_string() { return std::get<3>(*this); }
    //    auto& as_blob  () { return std::get<4>(*this); }
    //};

    using Value = std::variant<
        nullptr_t,
        int64_t,
        double,
        std::string,    // TODO: use unsigned char string as SQLite does ?
        Blob
    >;

    using Row = std::vector<Value>;

    using Rows = std::vector<Row>;

    template <typename Cargo> class Grouping_node;
    template <typename Cargo> class Row_packet_node;
    template <typename Cargo> class Row_content;

    template <typename Cargo>
    using Query_result_node =  std::variant<Grouping_node<Cargo>, Row_packet_node<Cargo>>;

    template <typename Cargo>
    class Grouping_node: public std::map<std::string, Query_result_node<Cargo>> {};

    template <typename Cargo>
    class Row_content: public std::vector<std::string> {
    public:
        auto& as_vector() { return *static_cast<std::vector<std::string>*>(this); }
        Cargo cargo;
    };

    template <typename Cargo>
    class Row_packet_node: public std::vector<Row_content<Cargo>> {};

    class Statement;


    class Database {

        // TODO: split into a Conan-specific derived class and a generic base class

    public:

        using select_callback = std::function<int(int col_count, const char* const col_names[], const char* const col_values[])>;

        Database(const char *filename);
        ~Database();

        void select(const char* statement, select_callback);

        auto select(
            std::initializer_list<std::string_view> columns,
            std::string_view table,
            std::string_view where_clause = "",
            std::string_view order_by_clause = "",
            std::initializer_list<std::string_view> params = {}
        ) -> Rows;

        auto select_one(std::string_view statement, const std::initializer_list<Value> keys = {}) -> Row;

        template <typename Cargo>
        auto get_tree(
            std::string_view table,
            std::initializer_list<std::string_view> group_by_cols,
            std::initializer_list<std::string_view> data_cols,
            std::string where_clause,
            std::string order_by_clause = ""
        ) -> Query_result_node<Cargo>;

        // Prepare a "select" statement.
        auto prepare_statement(
            std::string_view statement
        ) -> sqlite3_stmt*;

        // Prepare an "upsert" statement.
        auto prepare_upsert(
            std::string_view table,
            std::initializer_list<std::string_view> unique_columns  /* Columns that must be unique */,
            std::initializer_list<std::string_view> extra_columns   /* Columns without uniqueness */
        ) -> sqlite3_stmt*;

        // Execute a prepared statement. Call repeatedly as long as it returns true, calling get_row() every time to get row contents.
        // TODO: this is not ideal, as the parameters are only needed the first time.
        bool execute(sqlite3_stmt*, std::initializer_list<Value> values = {});

        void execute(std::string_view statement, std::string_view context = "");

        auto get_row(sqlite3_stmt*) -> Row;

    protected:
        
        auto handle() const { return db_handle; }

    private:

        static auto escape_single_quotes(std::string_view s) -> std::string;

        auto query_single_row(const char *list_query, const char *context = nullptr) -> std::vector<std::string>;

        [[deprecated]]
        void exec(const char *statement, std::string_view context = "");

        auto get_row_id(std::string_view table, std::string_view where_clause) -> int64_t;

        auto insert(std::string_view table, std::string_view columns, std::string_view values) -> int64_t;

        void drop_table(std::string_view version);

        sqlite3         *db_handle = nullptr;
        sqlite3_stmt    *stmt_upsert_package_description = nullptr;
    };

} // ns SQLite


// Inline implementations

#include <numeric>
#include <tuple>
#include "../string_utils.h"

namespace SQLite {

    template<typename Cargo>
    inline auto Database::get_tree(
        std::string_view table,
        std::initializer_list<std::string_view> group_by_cols,
        std::initializer_list<std::string_view> data_cols,
        std::string where_clause,
        std::string order_by_clause
    ) -> Query_result_node<Cargo>
    {
        using namespace std::string_literals;

        auto group_col_list = join_strings(group_by_cols);
        auto data_col_list = join_strings(data_cols);

        std::string statement = "select "s + group_col_list + ", " + data_col_list
            + " from " + std::string{ table }
            + (!where_clause.empty() ? " where "s + where_clause : ""s)
            + (!order_by_clause.empty() ? " order by "s + order_by_clause : ""s)
            ;

        Query_result_node<Cargo> root;

        select(statement.c_str(), [&](int col_count, const char* const col_values[], const char* const col_names[]) -> int {
            auto i = 0U;
            Query_result_node<Cargo> *node = &root;
            for (; i < group_by_cols.size(); i++) {
                const std::string key = col_values[i];
                if (key.empty())
                    continue;
                // Last grouping column ?
                if (node->index() != 0)
                    *node = Grouping_node<Cargo>{};
                node = &std::get<0>(*node)[key];
            }
            if (node->index() != 1)
                *node = Row_packet_node<Cargo>{};
            auto& row_packet = std::get<1>(*node);
            auto row = Row_content<Cargo>{};
            for (auto j = 0U; (i + j) < col_count; j++) {
                auto value = col_values[i + j];
                row.push_back(value ? value : "");
            }
            row_packet.emplace_back(std::move(row));
            return 0;
        });

        return root;
    }

} // ns Conan