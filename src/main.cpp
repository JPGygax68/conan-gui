#include <iostream>
#include <cstdio>
#include <map>
#include <imgui.h>
#include "./database.h"
#include "./repo_reader.h"
#include "./imgui_app.h"


using namespace Conan;

struct Filtered_package_list {
    std::future<Package_list>   future;
    bool                        acquired = false;
    Package_list                packages;
    std::future<void>           requery_future;
};

struct Package_tree  {
    std::map<char, Filtered_package_list>  letters;
    Package_tree() {
        for (auto letter = 'A'; letter <= 'Z'; letter++)
            letters[letter] = Filtered_package_list{};
    }
};


int main(int, char **)
{
    Conan::Database database;
    Conan::Repository_reader repo_reader{ database };

    std::future<void> queued_op;

    // auto package_list = database.get_package_list();
    auto tree = Package_tree{};

    imgui_init();

    //(void) initial_requeue.get();

    while (imgui_continue()) {
    
        imgui_new_frame();

        if (ImGui::Begin("Conan")) {
            if (ImGui::Button("Re-read all repositories")) {
                queued_op = repo_reader.requeue_all();
            }
            for (auto it = tree.letters.begin(); it != tree.letters.end(); it++) {
                ImGui::AlignTextToFramePadding();
                auto letter = std::string{it->first};
                if (ImGui::TreeNode(letter.c_str())) {
                    auto& sublist = it->second;
                    ImGui::SameLine();
                    if (sublist.requery_future.valid()) {
                        using namespace std::chrono_literals;
                        if (sublist.requery_future.wait_for(0us) == std::future_status::ready) {
                            (void) sublist.requery_future.get();
                            sublist.acquired = false;
                        } else
                            ImGui::TextUnformatted("(querying...)");
                    } else {
                        if (ImGui::Button("Re-query")) {
                            sublist.requery_future = std::async(std::launch::async, [=, &repo_reader]() {
                                repo_reader.filtered_read_all_repositories(std::string{letter});
                            });
                        }
                    }
                    if (!sublist.acquired) {
                        if (!sublist.future.valid()) {
                            sublist.future = std::async(std::launch::async, [=, &database]() { 
                                return database.get_package_list(letter); } );
                        }
                        using namespace std::chrono_literals;
                        auto result = sublist.future.wait_for(0s);
                        if (result == std::future_status::timeout) {
                            ImGui::TextUnformatted("(Querying...)");
                        }
                        else if (result == std::future_status::ready) {
                            sublist.packages = sublist.future.get();
                            sublist.acquired = true;
                        }
                    }
                    if (sublist.acquired) {
                        for (const auto& package: sublist.packages) {
                            ImGui::Selectable(package.c_str());
                        }
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