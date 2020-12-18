#include <ctype.h>
#include <imgui.h>
#include "alphabetic_tree.h"


Alphabetic_tree::Alphabetic_tree()
{
    query = database.prepare_statement(R"(
        SELECT id, name, packages2.remote, user, channel, version, description, license, provides, author, topics FROM packages2
        LEFT OUTER JOIN pkg_info ON pkg_info.pkg_id = packages2.id
        ORDER BY name COLLATE NOCASE ASC, packages2.remote COLLATE NOCASE ASC, user COLLATE NOCASE ASC, channel COLLATE NOCASE ASC,
            SEMVER_PART(version, 1) DESC, SEMVER_PART(version, 2) DESC, SEMVER_PART(version, 3) DESC, version DESC
    )");
}

void Alphabetic_tree::get_from_database()
{
    root.clear();

    while (database.execute(query)) {
        auto row = database.get_row(query);
        auto ch = toupper(std::get<3>(row[1])[0]);
        auto& letter  = root[ch];
        auto& package = letter .packages[std::get<3>(row[1])];
        auto& remote  = package.remotes [std::get<3>(row[2])];
        auto& user    = remote .users   [std::get<3>(row[3])];
        auto& channel = user   .channels[std::get<3>(row[4])];
        auto& version = channel.versions[std::get<3>(row[5])];
    }
}

void Alphabetic_tree::draw()
{
    for (auto& it : root) {
        draw_letter_node(it.first, it.second);
    }
}

void Alphabetic_tree::draw_letter_node(char letter, Letter_node& node)
{
    if (ImGui::TreeNode(std::string{letter}.c_str())) {
        for (auto& it : node.packages) {
            draw_package_variants(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_package_variants(const char* pkg_name, Package_variants_node& node)
{
    if (ImGui::TreeNode(pkg_name)) {
        for (auto& it : node.remotes) {
            draw_remote(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_remote(const char* name, Remote_node& node)
{
    if (ImGui::TreeNode(name)) {
        for (auto& it : node.users) {
            draw_user(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_user(const char* name, User_node& node)
{
    if (ImGui::TreeNode(name)) {
        for (auto& it: node.channels) {
            draw_channel(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_channel(const char* name, Channel_node& node)
{
    if (ImGui::TreeNode(name)) {
        for (auto& it : node.versions) {
            if (ImGui::TreeNode(it.first.c_str())) {
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
}
