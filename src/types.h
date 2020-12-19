#pragma once

#include <string>


struct Package_key {
    std::string remote;
    std::string package;
    std::string user;
    std::string channel;
    std::string version;
};

struct Package_info {
    std::string description;
    std::string license;
    std::string provides;
    std::string author;
    std::string topics;
    std::string creation_date;
};

