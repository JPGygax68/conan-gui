#include <iostream>
#include <cstdio>
#include <map>
#include <cassert>
#include <array>
#include <span>
#include <imgui.h>
#include <fmt/core.h>
#include "./cache_db.h"
#include "./repo_reader.h"
#include "./imgui_app.h"
#include "./alphabetic_tree.h"


using namespace Conan;


#ifdef OLD_CODE

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

#endif // OLD_CODE


int main(int, char **)
{
    try {

        Conan::Repository_reader repo_reader;

        imgui_init("Conan GUI");

        Alphabetic_tree alphabetic_tree{ repo_reader };
        alphabetic_tree.get_from_database();

        while (imgui_continue()) {
    
            imgui_new_frame();

            if (ImGui::Begin("Conan")) {
                alphabetic_tree.draw();
            }
            ImGui::End();

            imgui_frame_done();
        }
    }
    catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}