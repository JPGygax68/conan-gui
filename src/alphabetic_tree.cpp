#include <iostream>
#include <ctype.h>
#include <fmt/format.h>
#include <imgui.h>
#include "./repo_reader.h"
#include "./alphabetic_tree.h"


Alphabetic_tree::Alphabetic_tree(Conan::Repository_reader& rr):
    repo_reader{rr}
{
    // TODO: move to Cache_db
    info_query = database.prepare_statement(R"(
        SELECT remote, url, license, description, provides, author, topics, creation_date, last_poll
        FROM pkg_info
        WHERE pkg_id=?1
    )");
}

void Alphabetic_tree::get_from_database()
{
    root.clear();

    database.get_list(
        [this](SQLite::Row row) {
            auto ch = std::get<3>(row[1])[0];
            auto ch_uc = toupper(ch);
            auto& letter = root[ch_uc];
            add_row_to_package_list(letter.references, row);
            return true;
        }
    );
}

void Alphabetic_tree::draw()
{
    for (auto& it : root) {
        draw_letter_node(it.first, it.second);
    }
}

void Alphabetic_tree::add_row_to_package_list(References_list& pkg_list, const SQLite::Row& row)
{
    auto& package = pkg_list        [std::get<3>(row[1])];  // col 1: package name
    auto& remote  = package.remotes [std::get<3>(row[2])];  // col 2: remote
    auto& user    = remote .users   [std::get<3>(row[3])];  // col 3: user
    auto& channel = user   .channels[std::get<3>(row[4])];  // col 4: channel
    auto& version = channel.versions[std::get<3>(row[5])];  // col 5: version string

    version.description = std::get<3>(row[6]);
    version.pkg_id = std::get<1>(row[0]);
}

void Alphabetic_tree::draw_letter_node(char letter, Letter_node& node)
{
    using namespace std::chrono_literals;

    ImGui::AlignTextToFramePadding();
    auto open = ImGui::TreeNode(std::string{letter}.c_str());
    ImGui::SameLine();

    if (node.scan.valid()) {
        if (node.scan.wait_for(0s) == std::future_status::ready) {
            (void)node.scan.get();
            node.temp_packages.clear(); // just in case
            node.fetch = std::async(
                std::launch::async,
                [this, letter, &node]() {
                    Cache_db db;
                    db.get_list(
                        [this, &node](SQLite::Row row) {
                            add_row_to_package_list(node.temp_packages, row);
                            return true;
                        },
                        fmt::format("{0}%", letter)
                    );     
                }
            );
        }
    }
    if (!node.scan.valid()) {
        if (ImGui::Button("Re-scan")) {
            node.scan = std::async(
                std::launch::async, 
                [this, letter]() { repo_reader.read_letter_all_repositories(letter); }
            );
        }
    }
    else 
        ImGui::TextUnformatted("(scanning...)");

    if (node.fetch.valid()) {
        if (node.fetch.wait_for(0s) == std::future_status::ready) {
            (void)node.fetch.get();
            node.references = std::move(node.temp_packages);
        }
    }

    if (open) {
        for (auto& it : node.references) {
            package = it.first;
            draw_package_variants(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_package_variants(const char* pkg_name, Reference_node& node)
{
    if (ImGui::TreeNode(pkg_name)) {
        for (auto& it : node.remotes) {
            remote = it.first;
            draw_remote(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_remote(const char* name, Remote_node& node)
{
    if (ImGui::TreeNode(name)) {
        for (auto& it : node.users) {
            user = it.first;
            draw_user(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_user(const char* name, User_node& node)
{
    if (ImGui::TreeNode(name)) {
        for (auto& it: node.channels) {
            channel = it.first;
            draw_channel(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_channel(const char* name, Channel_node& node)
{
    if (ImGui::TreeNode(name)) {
        for (auto& it : node.versions) {
            version = it.first;
            draw_version(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_version(const char* name, Version_node& node)
{
    ImGui::PushID(name);

    ImGui::AlignTextToFramePadding();
    auto open = ImGui::TreeNode(name);

    ImGui::SameLine();

    auto requery = false;
    if (node.pkg_info.busy() && !node.pkg_info.ready()) {
        ImGui::TextUnformatted("(Querying...)");
    }
    else {
        requery = ImGui::Button("Re-query");
        if (requery)
            node.pkg_info.reset();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(node.description.c_str());

    if (requery || open && !node.pkg_info.ready()) {
        if (node.pkg_info.blank()) {
            node.pkg_info.obtain(
                [this, &node](const Package_key key, int64_t pkg_id, bool requery) {
                    Cache_db db;
                    std::optional<Package_info> info;
                    if (!requery) {
                        info = db.get_package_info(pkg_id);
                    }
                    if (!info) {
                        info = repo_reader.get_info(key);
                        node.description = info->description;
                        db.upsert_package_info(pkg_id, *info);
                    }
                    return *info;
                },
                Package_key{ remote, package, user, channel, version },
                node.pkg_id,
                requery || node.description.empty()
            );
        }
    }

    if (open) {
        if (node.pkg_info.ready()) {
            const auto& info = node.pkg_info.value();
            ImGui::Text("License: %s", info.license.c_str());
        }
        else {
            ImGui::TextUnformatted("(please wait...)");
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}
