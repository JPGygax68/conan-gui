#pragma once

#include <map>
#include <string>
#include <future>
#include "./async_data.h"
#include "./types.h"
#include "./cache_db.h"


struct Alphabetic_tree {

    struct Package_variants_node;
    struct Remote_node;
    struct User_node;
    struct Channel_node;
    struct Version_node;

    struct Letter_node {
        std::map<std::string, Package_variants_node> packages;
    };

    struct Package_variants_node {
        std::map<std::string, Remote_node> remotes;
    };

    struct Remote_node {
        std::map<std::string, User_node> users;
    };

    struct User_node {
        std::map<std::string, Channel_node> channels;
    };

    struct Channel_node {
        std::unordered_map<std::string, Version_node> versions;
    };

    struct Version_node {
        uint64_t pkg_id = 0;
        std::string description;
        async_data<Package_info> pkg_info;
    };

    std::map<char, Letter_node> root;

    explicit Alphabetic_tree(Conan::Repository_reader&);

    void get_from_database();

    void draw();

private:

    void draw_letter_node(char letter, Letter_node& node);
    void draw_package_variants(const char* pkg_name, Package_variants_node& node);
    void draw_remote(const char* name, Remote_node& node);
    void draw_user(const char* name, User_node& node);
    void draw_channel(const char* name, Channel_node& node);
    void draw_version(const char* name, Version_node& node);

    Conan::Repository_reader&   repo_reader;
    Cache_db                    database;
    sqlite3_stmt*               list_query = nullptr; // TODO: move to Cache_db 
    sqlite3_stmt*               info_query = nullptr; // ditto

    std::string                 remote, package, user, channel, version;
    // uint64_t                    pkg_id = {};
};

