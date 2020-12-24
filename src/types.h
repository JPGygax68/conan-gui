#pragma once

#include <vector>
#include <string>


struct Package_reference {
    std::string package;
    std::string user;
    std::string channel;
    std::string version;
};

struct Package_key {
    std::string remote;
    Package_reference reference;
};

struct Package_info {
    std::string description;
    std::string license;
    std::string provides;
    std::string author;
    std::vector<std::string> topics;
    std::string creation_date;
};

