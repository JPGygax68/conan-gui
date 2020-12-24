#include <iostream>
#include <ranges>
#include <algorithm>
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

    SQLite::Row prev_row = { {" "}, {" "}, {" "}, {" "}, {" "}, {" "} };

    database.get_list(
        [this, &prev_row](SQLite::Row row) {
            auto ch = std::get<3>(row[1])[0];
            auto ch_uc = toupper(ch);
            auto& letter = root[ch_uc];
            add_row_to_references_list(letter.references, row, prev_row);
            prev_row = row;
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

void Alphabetic_tree::add_row_to_references_list(References_list& ref_list, const SQLite::Row& row, const SQLite::Row& prev_row)
{
    auto& reference = std::get<3>(row[1]);
    auto& remote    = std::get<3>(row[2]);
    auto& user      = std::get<3>(row[3]);
    auto& channel   = std::get<3>(row[4]);
    auto& version   = std::get<3>(row[5]);

    bool changed = false;
    if (reference != std::get<3>(prev_row[1])) changed = true;
    if (changed) ref_list.push_back({ reference, Reference_node{} });
    auto& remote_list = ref_list.back().second.remotes;
    changed = changed || remote != std::get<3>(prev_row[2]);
    if (changed) remote_list.push_back({ remote, Remote_node{} });
    auto& user_list = remote_list.back().second.users;
    changed = changed || user != std::get<3>(prev_row[3]);
    if (changed) user_list.push_back({ user, User_node{} });
    auto& channel_list = user_list.back().second.channels;
    changed = changed || channel != std::get<3>(prev_row[4]);
    if (changed) channel_list.push_back({ channel, Channel_node() });
    auto& package_list = channel_list.back().second.packages;
    changed = changed || version != std::get<3>(prev_row[5]);
    if (changed) package_list.push_back({});

    auto& package_node = package_list.back();

    package_node.pkg_id = std::get<1>(row[0]);
    package_node.version = version;
    package_node.description = std::get<3>(row[6]);
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
                    SQLite::Row prev_row = { {" "}, {" "}, {" "}, {" "}, {" "}, {" "} };
                    db.get_list(
                        [this, &node, &prev_row](SQLite::Row row) {
                            add_row_to_references_list(node.temp_packages, row, prev_row);
                            prev_row = row;
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
            draw_reference(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_reference(const char* pkg_name, Reference_node& node)
{
    if (ImGui::TreeNode(pkg_name)) {
        for (auto& it : node.remotes) {
            remote = it.first;
            draw_remote(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_remote(const char* version, Remote_node& node)
{
    if (ImGui::TreeNode(version)) {
        for (auto& it : node.users) {
            user = it.first;
            draw_user(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_user(const char* version, User_node& node)
{
    if (ImGui::TreeNode(version)) {
        for (auto& it: node.channels) {
            channel = it.first;
            draw_channel(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_channel(const char* version, Channel_node& node)
{
    if (ImGui::TreeNode(version)) {
        for (auto& package: node.packages) {
            draw_package(package);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_package(Package_node& node)
{
    version = node.version;

    ImGui::PushID(version.c_str());

    ImGui::AlignTextToFramePadding();
    auto open = ImGui::TreeNode(node.version.c_str());

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
