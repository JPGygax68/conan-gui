#pragma once

#include <vector>
#include <forward_list>
#include <string>
#include <future>
#include "./async_data.h"
#include "./types.h"
#include "./cache_db.h"


struct Alphabetic_tree {

    struct Reference_node;
    struct Remote_node;
    struct User_node;
    struct Channel_node;
    struct Package_node;

    using References_list = std::vector<std::pair<std::string, Reference_node>>;

    struct Letter_node {
        References_list references;
        std::future<void> scan;     // Repo Reader
        std::future<void> fetch;    // Cache DB
        References_list temp_packages;
    };

    struct Reference_node {
        std::vector<std::pair<std::string, Remote_node>> remotes;
        Reference_node(Reference_node&&) = default;
        Reference_node& operator = (Reference_node&&) = default;
        Reference_node() = default;
    };

    struct Remote_node {
        std::vector<std::pair<std::string, User_node>> users;
        Remote_node(Remote_node&&) = default;
        Remote_node& operator = (Remote_node&&) = default;
        Remote_node() = default;
    };

    struct User_node {
        std::vector<std::pair<std::string, Channel_node>> channels;
        User_node(User_node&&) = default;
        User_node& operator = (User_node&&) = default;
        User_node() = default;
    };

    struct Channel_node {
        std::vector<Package_node> packages;
        Channel_node(Channel_node&&) = default;
        Channel_node& operator = (Channel_node&&) = default;
        Channel_node() = default;
    };

    struct Package_node {
        int64_t pkg_id = 0;
        std::string version;
        std::optional<Package_info> pkg_info; // TODO: rename ?
        std::promise<Package_info> get_info_prom;
        std::future<Package_info> get_info_fut;
        // async_data<Package_info> pkg_info;

        Package_node(Package_node&& src) noexcept :
            pkg_id{std::move(src.pkg_id)},
            version{std::move(src.version)},
            pkg_info{std::move(src.pkg_info)},
            get_info_fut{std::move(src.get_info_fut)}
        {}
        Package_node& operator = (Package_node&&) noexcept = default;
        Package_node() = default;
    };

    explicit Alphabetic_tree(Conan::Repository_reader&);

    void get_from_database();

    void draw();

private:

    void add_row_to_references_list(References_list& pkg_list, const SQLite::Row& row, const SQLite::Row& prev_row);

    void draw_letter_node(char letter, Letter_node& node);
    void draw_reference(const char* pkg_name, Reference_node& node);
    void draw_remote(const char* version, Remote_node& node);
    void draw_user(const char* version, User_node& node);
    void draw_channel(const char* version, Channel_node& node);
    void draw_package(Package_node& node);

    Conan::Repository_reader&   repo_reader;
    Cache_db                    database;
    sqlite3_stmt*               info_query = nullptr; // ditto

    std::map<char, Letter_node> root;

    std::string                 remote, package, user, channel, version;
    // uint64_t                    pkg_id = {};

    std::future<void>           full_scan;
};

