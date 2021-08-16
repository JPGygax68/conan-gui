#include <algorithm>
#include <cstdio>
#include <iostream>
#include <future>
#include <regex>
#include <cassert>
#include <format>
#include "./cache_db.h"
#include "./repo_reader.h"


namespace Conan {
    
    Repository_reader::Repository_reader()
    {
        remotes_ad.obtain([]() {
            auto file_ptr = _popen("conan remote list", "r");
            if (!file_ptr) throw std::system_error(errno, std::generic_category());
            std::vector<std::string> list;
            while (!feof(file_ptr)) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), file_ptr)) {
                    std::string input = buffer;
                    input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                    auto version = input.substr(0, input.find(":"));
                    std::cout << version << std::endl; // TODO: replace with log
                    list.push_back(version);
                }
            }
            fclose(file_ptr);
            return list;
        });
    }
    
    void Repository_reader::filtered_read(std::string_view remote, std::string_view name_filter)
    {
        update_package_list(remote, name_filter);
    }

    void Repository_reader::read_letter_all_repositories(char first_letter)
    {
        assert(first_letter >= 'A' && first_letter <= 'Z');

        auto& remotes = remotes_ad.get();
        for (auto& remote: remotes) {
            filtered_read(remote, std::format("{:c}*", first_letter));
            filtered_read(remote, std::format("{:c}*", first_letter + 'a' - 'A'));
        }
    }

    auto Repository_reader::get_info(const Package_key& key) -> Package_info
    {
        std::string specifier = std::format("{0}/{1}@", key.reference.package, key.reference.version);
        if (!key.reference.user.empty()) specifier += std::format("{0}/{1}", key.reference.user, key.reference.channel);
            
        // auto cmd = fmt::format("conan info -r {0} {1}", remote, specifier);
        auto cmd = std::format("conan inspect -r {0} {1}", key.remote, specifier);
        std::cout << "INSPECT command: " << cmd << std::endl;
        auto file_ptr = _popen(cmd.c_str(), "r");
        if (!file_ptr) throw std::system_error(errno, std::generic_category());

        // auto re = std::regex("^[ \t]+([^:]+):[ \t]*(.*)$");
        auto re = std::regex("^([^:]+):[ \t]*(.*)$");

        Package_info info;

        while (!feof(file_ptr)) {
            char buffer[1024];
            if (fgets(buffer, sizeof(buffer), file_ptr)) {
                std::string input = buffer;
                input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                std::cout << input << std::endl;
                std::smatch m;
                if (std::regex_match(input, m, re)) {
                    // if (m[1] == "Description") info.description = m[2];
                    if      (m[1] == "description") info.description = m[2];
                    else if (m[1] == "license"    ) info.license     = m[2];
                    else if (m[1] == "provides"   ) info.provides    = m[2];
                    else if (m[1] == "author"     ) info.author      = m[2];
                    else if (m[1] == "topics"     ) info.topics      = parseTagList(m[2].str());
                }
                else {
                    std::cerr << "***FAILED to parse info line \"" << input << "\"" << std::endl;
                }
            }
        }

        fclose(file_ptr);

        // database.set_package_info(pkg_id, info); // TODO: replace with Database::upsert()

        return info;
    }

    void Repository_reader::update_package_list(std::string_view remote, std::string_view name_filter) 
    {
        auto file_ptr = _popen(std::format("conan search -r {} {}* --raw", remote, name_filter).c_str(), "r");
        if (!file_ptr) throw std::system_error(errno, std::generic_category());

        Cache_db db;

        auto re = std::regex("([^/]+)/([^@]+)(?:@([^/]+)/(.+))?");

        while (!feof(file_ptr)) {
            char buffer[1024];
            if (fgets(buffer, sizeof(buffer), file_ptr)) {
                std::string input = buffer;
                input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
                std::cout << input << std::endl;
                std::smatch m;
                if (std::regex_match(input, m, re)) {
                    std::cout << "Package name: " << m[1] << ", version: " << m[2] << ", user: " << m[3] << ", channel: " << m[4] << std::endl;
                    db.upsert_package(remote, m[1].str(), m[2].str(), m[3].str(), m[4].str());
                } else {
                    std::cerr << "***FAILED to parse package specifier \"" << input << "\"" << std::endl;
                }
            }
        }

        fclose(file_ptr);
    }

} // Conan
