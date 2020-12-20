#include <ctype.h>
#include <imgui.h>
#include "./repo_reader.h"
#include "./alphabetic_tree.h"


Alphabetic_tree::Alphabetic_tree(Conan::Repository_reader& rr):
    repo_reader{rr}
{
    // TODO: move to Cache_db
    list_query = database.prepare_statement(R"(
        SELECT id, name, packages2.remote, user, channel, version, description, license, provides, author, topics FROM packages2
        LEFT OUTER JOIN pkg_info ON pkg_info.pkg_id = packages2.id
        ORDER BY name COLLATE NOCASE ASC, packages2.remote COLLATE NOCASE ASC, user COLLATE NOCASE ASC, channel COLLATE NOCASE ASC,
            SEMVER_PART(version, 1) DESC, SEMVER_PART(version, 2) DESC, SEMVER_PART(version, 3) DESC, SEMVER_PART(version, 4), version DESC
    )");

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

    while (database.execute(list_query)) {
        auto row = database.get_row(list_query);
        auto ch = toupper(std::get<3>(row[1])[0]);
        auto& letter  = root[ch];
        auto& package = letter .packages[std::get<3>(row[1])];
        auto& remote  = package.remotes [std::get<3>(row[2])];
        auto& user    = remote .users   [std::get<3>(row[3])];
        auto& channel = user   .channels[std::get<3>(row[4])];
        auto& version = channel.versions[std::get<3>(row[5])];
        version.description = std::get<3>(row[6]);
        version.pkg_id = std::get<1>(row[0]);
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
            package = it.first;
            draw_package_variants(it.first.c_str(), it.second);
        }
        ImGui::TreePop();
    }
}

void Alphabetic_tree::draw_package_variants(const char* pkg_name, Package_variants_node& node)
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
    if (node.pkg_info_ad.busy() && !node.pkg_info_ad.ready()) {
        ImGui::TextUnformatted("(Querying...)");
    }
    else {
        requery = ImGui::Button("Re-query");
        if (requery)
            node.pkg_info_ad.reset();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(node.description.c_str());

    if (requery || open && !node.pkg_info_ad.ready()) {
        if (node.pkg_info_ad.blank()) {
            node.pkg_info_ad.obtain(
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
        if (node.pkg_info_ad.ready()) {
            const auto& info = node.pkg_info_ad.value();
            ImGui::Text("License: %s", info.license.c_str());
        }
        else {
            ImGui::TextUnformatted("(please wait...)");
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}
