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

namespace Conan {

    // TODO: move to non-specific base module

    class Grouping_node;
    class Row_packet_node;
    class Column_values;
    using Query_result_node = std::variant<Grouping_node, Row_packet_node>;
    class Grouping_node: public std::map<std::string, Query_result_node> {};
    class Column_values: public std::vector<std::string> {};
    class Row_packet_node: public std::vector<Column_values> {};
    
    // class Query_result_node: public std::variant<Grouping_node, Column_values> {
    // public:
    //     bool is_leaf() const { return index() == 1; }
    //     auto child_group() -> Grouping_node& { return std::get<0>(*this); }
    //     auto column_values() -> Column_values& { return std::get<1>(*this); }
    // };

    // Conan-specific 

    struct Package_designator {
        std::string             name;
        std::string             repository;
        // TODO: more ? queries ?
    };

    using Package_designators = std::vector<Package_designator>;

    struct Version {
        std::string description;
        // TODO: lots more data returned by "info" command
        // TODO: "last_seen" date+time
    };

    struct Channel {
        std::map<std::string, Version> versions;
    };

    struct User {
        std::map<std::string, Channel> channels;
    };

    struct Package {
        std::string remote;
        std::map<std::string, User> users;
    };


    class Database {

        // TODO: split into a Conan-specific derived class and a generic base class

    public:

        Database();
        ~Database();

        // static auto& instance() { static Database db; return db; } // BAD IDEA! CONCURRENCY!

        void upsert_package(std::string_view remote, const std::string& reference);

        void upsert_package2(std::string_view remote, std::string_view name, std::string_view version, std::string_view user, std::string_view channel);

        // TODO: replace with more structured data
        auto get_package_designators(std::string_view name_filter) -> Package_designators;

        auto get_package(std::string_view remote, std::string_view pkg_name) -> Package;

        auto get_tree(
            std::string_view table,
            std::initializer_list<std::string_view> group_by_cols,
            std::initializer_list<std::string_view> data_cols, 
            std::string where_clause
        ) -> Query_result_node;

    private:

        using select_callback = std::function<int(int col_count, const char * const col_names[], const char * const col_values[])>;

        auto query_single_row(const char *query, const char *context = nullptr) -> std::vector<std::string>;

        void exec(const char *statement, std::string_view context = "");

        void select(const char *statement, select_callback);
        auto get_row_id(std::string_view table, std::string_view where_clause) -> int64_t;
        auto insert(std::string_view table, std::string_view columns, std::string_view values) -> int64_t;
        void drop_table(std::string_view name);

        sqlite3     *db_handle = nullptr;
    };

} // ns Conan