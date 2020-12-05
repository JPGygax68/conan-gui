#include <iostream>
#include <cstdio>
#include <map>
#include <cassert>
#include <imgui.h>
#include <fmt/core.h>
#include "./database.h"
#include "./repo_reader.h"
#include "./imgui_app.h"


using namespace Conan;

using Package_map = std::map<std::string, Package>;         // { remote, name}  -> package contents

struct Filtered_packages {
#ifdef OLD_CODE
    std::future<Package_map>            query_future;       // querying from sqlite
    Package_map                         packages;
#else 
    std::future<Query_result_node>      query_future;       // querying from sqlite
    Query_result_node                   root_node;
#endif
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
    const Query_result_node& node,
    size_t group_levels,
    std::function<void(const Column_values&, int)> row_presenter
) {
    if (group_levels == 0) {
        assert(node.index() == 1);
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
    Conan::Database database;
    Conan::Repository_reader repo_reader{ database };

    std::future<void> queued_op;

    // auto package_list = database.get_package_list();
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
                        if (!sublist.query_future.valid()) {
                            sublist.query_future = std::async(std::launch::async, [=, &database]() {
                                auto where_clause = fmt::format("name like '{0}%'", letter);
                                auto node = database.get_tree(
                                    "packages2 LEFT OUTER JOIN pkg_info ON pkg_info.pkg_id = packages2.id", 
                                    { "name", "packages2.remote", "user", "channel", "version" },
                                    { "description", "pkg_id", "name", "version", "user", "channel" }, 
                                    where_clause
                                );
                                return node;
                            });
                        }
                        using namespace std::chrono_literals;
                        auto result = sublist.query_future.wait_for(0s);
                        if (result == std::future_status::timeout) {
                            ImGui::TextUnformatted("(Querying...)");
                        }
                        else if (result == std::future_status::ready) {
                            sublist.root_node = sublist.query_future.get();
                            sublist.acquired = true;
                        }
                    }
                    if (sublist.acquired) {
                        show_query_result_node(sublist.root_node, 4, [&](const Column_values& row, int ridx) {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(row[0].c_str());
                            ImGui::SameLine();
                            if (ImGui::Button("Get Info")) {
                                // repo_reader.get_package_info(row[1], row[2], row[3], row[4]);
                            }
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