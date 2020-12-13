#include <iostream>
#include <cstdio>
#include <map>
#include <cassert>
#include <array>
#include <span>
#include <imgui.h>
#include <fmt/core.h>
#include "./sqlite/database.h"
#include "./repo_reader.h"
#include "./imgui_app.h"


using namespace Conan;

using Package_info_async = async_data<Package_info>;
using Package_group_node = SQLite::Query_result_node<Package_info_async>;
using Packages_root = Package_group_node;

using Package_row = SQLite::Row_content<Package_info_async>;

struct Filtered_packages {
    std::future<Packages_root>          packages_future;    // querying from sqlite
    Packages_root                       packages_root;
    bool                                acquired = false;   // TODO: replace with timestamp ?
    std::future<void>                   refresh_future;     // re-querying from Conan
};

struct Package_tree  {
    std::map<char, Filtered_packages>  letters;
    Package_tree() {
        for (auto letter = 'A'; letter <= 'Z'; letter++)
            letters[letter] = Filtered_packages{};
    }
};


static void show_query_result_node(
    Package_group_node& node,
    size_t group_levels,
    std::function<void(Package_row& row, int)> row_presenter
) {
    if (node.index() == 1 /* group_levels == 0 */) {
        // assert(node.index() == 1);
        auto& rows = std::get<1>(node);
        for (auto i = 0U; i < rows.size(); i++) {
            auto& row = rows[i];
            row_presenter(row, i);
            i++;
        }
    }
    else {
        assert(node.index() == 0);
        auto& group = std::get<0>(node);
        for (auto& it: group) {
            auto name = it.first.empty() ? "(none)" : it.first.c_str();
            if (ImGui::TreeNode(name)) {
                show_query_result_node(it.second, group_levels - 1, row_presenter);
                ImGui::TreePop();
            }
        }
    }
}


int main(int, char **)
{
    SQLite::Database database;
    Conan::Repository_reader repo_reader{ database };

    std::future<void> queued_op;

    // auto package_list = database.update_package_list();
    auto tree = Package_tree{};

    imgui_init("Conan GUI");

    while (imgui_continue()) {
    
        imgui_new_frame();

        if (ImGui::Begin("Conan")) {
            if (ImGui::Button("Re-read all repositories")) {
                queued_op = repo_reader.requeue_all();
            }
            for (auto it = tree.letters.begin(); it != tree.letters.end(); it++) {
                ImGui::AlignTextToFramePadding();
                auto letter = it->first;
                if (ImGui::TreeNodeEx(std::string{letter}.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& sublist = it->second;
                    ImGui::SameLine();
                    if (sublist.refresh_future.valid()) {
                        using namespace std::chrono_literals;
                        if (sublist.refresh_future.wait_for(0us) == std::future_status::ready) {
                            (void) sublist.refresh_future.get();
                            sublist.acquired = false;
                        } else
                            ImGui::TextUnformatted("(querying...)");
                    } else {
                        if (ImGui::Button("Re-query")) {
                            sublist.refresh_future = std::async(std::launch::async, [=, &repo_reader]() {
                                repo_reader.read_letter_all_repositories(letter);
                            });
                        }
                    }
                    if (!sublist.acquired) {
                        if (!sublist.packages_future.valid()) {
                            sublist.packages_future = std::async(std::launch::async, [=, &database]() {
                                auto node = database.get_tree<Package_info_async>(
                                    "packages2 LEFT OUTER JOIN pkg_info ON pkg_info.pkg_id = packages2.id", 
                                    { "name", "packages2.remote", "user", "channel" },
                                    { "packages2.id, packages2.remote, name, version, user, channel, description, license, provides, author, topics" }, 
                                    fmt::format("name like '{0}%'", std::string{letter}),
                                    "name, packages2.remote, user, channel, SEMVER_PART(version, 1) DESC, SEMVER_PART(version, 2) DESC, SEMVER_PART(version, 3) DESC, version DESC"
                                );
                                return node;
                            });
                        }
                        using namespace std::chrono_literals;
                        auto result = sublist.packages_future.wait_for(0s);
                        if (result == std::future_status::timeout) {
                            ImGui::TextUnformatted("(Querying...)");
                        }
                        else if (result == std::future_status::ready) {
                            sublist.packages_root = sublist.packages_future.get();
                            sublist.acquired = true;
                        }
                    }
                    if (sublist.acquired) {
                        show_query_result_node(sublist.packages_root, 4, [&](SQLite::Row_content<Package_info_async>& row, int ridx) {
                            int64_t id = std::stoll(row[0]);
                            auto& remote = row[1];
                            auto& name = row[2];
                            auto& version = row[3];
                            auto& user = row[4];
                            auto& channel = row[5];
                            auto& description = row[6];
                            ImGui::AlignTextToFramePadding();
                            auto x = ImGui::GetCursorPosX();
                            auto open = ImGui::TreeNode(version.c_str());
                            ImGui::SameLine();
                            ImGui::SetCursorPosX(x + imgui_default_font_size() * 8);
                            ImGui::PushID(version.c_str());
                            if (!description.empty()) {
                                if (ImGui::Button("Re-query")) {
                                    std::cout << "Re-querying..." << std::endl;
                                    description = "";
                                    row.cargo.reset();
                                }
                                ImGui::SameLine();
                                ImGui::TextWrapped("%s", description.c_str());
                            }
                            if (description.empty()) {
                                if (row.cargo.ready()) {
                                    std::cout << "Obtained description: " << row.cargo.value().description << std::endl;
                                    auto description = row.cargo.value().description;
                                    database.set_package_description(row[0], row.cargo.value().description);
                                    if (description.empty()) description = "(Failed to obtain package info)"; // TODO: this is a stopgap, need better handling
                                    //row[6] = description; // so we don't have to re-query
                                    row[6] = ""; // trigger a re-query from the database TODO: does not work that way
                                    ImGui::NewLine();
                                }
                                else {
                                    if (row.cargo.busy()) {
                                        ImGui::TextUnformatted("(Querying...)");
                                    }
                                    else {
                                        row.cargo.obtain([&]() {
                                            return repo_reader.get_info(remote, name, version, user, channel, id);
                                        });
                                    }
                                }
                            }
                            else {
                                if (open) {
                                    ImGui::Text("License: %15s  Provides: %30s  Author: %30s", row[7].c_str(), row[8].c_str(), row[9].c_str());
                                    ImGui::Text("Topics: %s", row[10].c_str());
                                }
                            }
                            ImGui::PopID();
                            if (open) ImGui::TreePop();
                        });
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::End();

        imgui_frame_done();
    }

    return 0;
}