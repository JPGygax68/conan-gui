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

    struct Package_info {
        std::string description;
        std::string license;
        std::string provides;
        std::string author;
        std::string topics;
        std::string creation_date;
    };


    class Database {

        // TODO: split into a Conan-specific derived class and a generic base class

    public:

        using select_callback = std::function<int(int col_count, const char* const col_names[], const char* const col_values[])>;

        Database();
        ~Database();

        // static auto& instance() { static Database db; return db; } // BAD IDEA! CONCURRENCY!

        void upsert_package(std::string_view remote, const std::string& reference);

        void upsert_package2(std::string_view remote, std::string_view name, std::string_view version, std::string_view user, std::string_view channel);

        // TODO: replace with more structured data
        auto get_package_designators(std::string_view name_filter) -> Package_designators;

        void set_package_description(std::string_view id, std::string_view description);
        void set_package_info(sqlite3_int64 pkg_id, const Package_info& info);

        // auto get_package(std::string_view remote, std::string_view pkg_name) -> Package_row;

        void select(const char* statement, select_callback);

        template <typename Cargo>
        auto get_tree(
            std::string_view table,
            std::initializer_list<std::string_view> group_by_cols,
            std::initializer_list<std::string_view> data_cols, 
            std::string where_clause,
            std::string order_by_clause = ""
        ) -> Query_result_node<Cargo>;

    private:

        static auto escape_single_quotes(std::string_view s) -> std::string;

        auto query_single_row(const char *query, const char *context = nullptr) -> std::vector<std::string>;

        void exec(const char *statement, std::string_view context = "");

        auto get_row_id(std::string_view table, std::string_view where_clause) -> int64_t;
        auto insert(std::string_view table, std::string_view columns, std::string_view values) -> int64_t;
        void drop_table(std::string_view name);

        sqlite3         *db_handle = nullptr;
        sqlite3_stmt    *stmt_upsert_package_description = nullptr;
    };

} // ns Conan


// Inline implementations

#include <numeric>
#include <tuple>

// Utilities (TODO: move?)

template <typename Seq> // requires sequence_of_convertibles_to_string<Seq>
auto join_strings(Seq strings, std::string_view separator = ",") -> std::string
{
    return std::accumulate(strings.begin(), strings.end(), std::string(),
        [=](auto a, auto b) { return std::string{ a } + std::string{ a.length() > 0 ? separator : "" } + std::string{ b }; });
}

namespace Conan {

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