#include <iostream>
#include <ranges>
#include <algorithm>
#include <ctype.h>
#include <format>
#include <imgui.h>
#include "./string_utils.h"
#include "./repo_reader.h"
#include "./job_queue.h"
#include "./gui_elements.h"
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
    for (char letter = 'A'; letter <= 'Z'; letter++) root[letter] = {};

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
    if (!full_scan.running()) {
        if (ImGui::Button("Re-read all repositories")) {
            // TODO: queue scans for all nodes
        }
    } else {
        gui::FormattedText("Full-scan underway (scanning letter {:c})", full_scan.current_letter.load());
    }

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

    // Do we have description (non-null) ? then we have the package info
    if (row[6].index() == 3) {
        package_node.pkg_info = Package_info {
            .description = std::get<3>(row[6]),
            .license     = row[ 7].index() == 0 ? "" : std::get<3>(row[7]),
            .provides    = row[ 8].index() == 0 ? "" : std::get<3>(row[8]),
            .author      = row[ 9].index() == 0 ? "" : std::get<3>(row[9]),
            .topics      = parseTagList(row[10].index() == 3 ? std::get<3>(row[10]) : ""),
            // .creation_date = std::get<3>(row[11])
        };
    }
}

void Alphabetic_tree::draw_letter_node(char letter, Letter_node& node)
{
    // using namespace std::chrono_literals;

    ImGui::AlignTextToFramePadding();
    auto open = ImGui::TreeNode(std::string{letter}.c_str());
    ImGui::SameLine();

    // Are we scanning this letter ?
    if (node.scanning()) {
        if (node.scan_done()) {
            (void)node.scan.get();
            node.temp_packages.clear(); // just in case
            // Start the database fetch operation
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
                        std::format("{0}%", letter)
                    );     
                }
            );
        }
    }
    if (!full_scan.running()) {
        if (!node.scanning()) {
            if (ImGui::Button("Re-scan")) {
                node.scan = std::async(
                    std::launch::async, 
                    [this, letter]() { 
                        repo_reader.read_letter_all_repositories(letter);
                        database.mark_letter_as_scanned(letter);
                    }
                );
            }
        }
        else 
            ImGui::TextUnformatted("(scanning...)");
    }
    else 
        ImGui::TextUnformatted("(Full scan running...)");

    if (node.fetching()) {
        if (node.fetching_done()) {
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

    bool requery = false;

    ImGui::SameLine();
    if (full_scan.future.valid())
        ImGui::TextUnformatted("(Full scan running...)");
    else {
        if (node.get_info_fut.valid()) {
            ImGui::TextUnformatted("(Querying...)");
        }
        else {
            requery = ImGui::Button("Re-query");
        }
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(node.pkg_info ? node.pkg_info->description.c_str() : "(please wait...)");

    if (open) {
        if (node.pkg_info) {
            const auto& info = node.pkg_info.value();
            ImGui::Text("License: %s", info.license.c_str());
            ImGui::Text("Topics: %s", join_strings(info.topics, ", ").c_str());
        }
        else {
            ImGui::TextUnformatted("(please wait...)");
        }
        ImGui::TreePop();
    }

    if (requery || !node.pkg_info) {
        if (requery) 
            node.get_info_fut = {};
        if (!node.get_info_fut.valid()) {
            node.get_info_fut = node.get_info_prom.get_future();
            Job_queue::instance().queue_job(
                [this](Package_key key, int64_t pkg_id, std::promise<Package_info>& promise) {
                    return [this, key, pkg_id, &promise]() {
                        auto info = repo_reader.get_info(key);
                        Cache_db db;
                        db.upsert_package_info(pkg_id, info);
                        promise.set_value(info);
                    };
                } (
                    Package_key{ remote, package, user, channel, version },
                    node.pkg_id,
                    node.get_info_prom
                )
            );
            // node.get_info_fut = std::async(
            //     std::launch::async,
            //     [this, &node](const Package_key key, int64_t pkg_id) {
            //         auto info = repo_reader.get_info(key);
            //         Cache_db db;
            //         db.upsert_package_info(pkg_id, info);
            //         return info;
            //     },
            //     Package_key{ remote, package, user, channel, version },
            //     node.pkg_id
            // );
        }
    }

    if (node.get_info_fut.valid() && node.get_info_fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        node.pkg_info = node.get_info_fut.get();
    }

    ImGui::PopID();
}
