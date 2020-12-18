#pragma once

#include <map>
#include <string>
#include "./cache_db.h"


struct Alphabetic_tree {

    struct Package_variants_node;
    struct Remote_node;
    struct User_node;
    struct Channel_node;
    struct Version_node;

    struct Letter_node {
        // char letter;
        std::map<std::string, Package_variants_node> packages;
    };

    struct Package_variants_node {
        // std::string name;
        std::map<std::string, Remote_node> remotes;
    };

    struct Remote_node {
        // std::string name;
        std::map<std::string, User_node> users;
    };

    struct User_node {
        // std::string name;
        std::map<std::string, Channel_node> channels;
    };

    struct Channel_node {
        // std::string name;
        std::map<std::string, Version_node> versions;
    };

    struct Version_node {
        // std::string version;
        // TODO: promise for package info ?
    };

    std::map<char, Letter_node> root;

    Alphabetic_tree();

    void get_from_database();

    void draw();

private:

    void draw_letter_node(char letter, Letter_node& node);
    void draw_package_variants(const char* pkg_name, Package_variants_node& node);
    void draw_remote(const char* name, Remote_node& node);
    void draw_user(const char* name, User_node& node);
    void draw_channel(const char* name, Channel_node& node);

    Cache_db        database;
    sqlite3_stmt*   query = nullptr;
};

