#include <iostream>
#include <cstdio>
#include <map>
#include <imgui.h>
#include "./database.h"
#include "./repo_reader.h"
#include "./imgui_app.h"


using namespace Conan;

struct Filtered_package_list {
    std::future<Package_list>   query_future;       // querying from sqlite
    bool                        acquired = false;   // TODO: replace with timestamp ?
    Package_list                packages;
    std::future<void>           refresh_future;     // re-querying from Conan
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

    imgui_init("Conan GUI");

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
                if (ImGui::TreeNodeEx(letter.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
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
                                repo_reader.filtered_read_all_repositories(std::string{letter});
                            });
                        }
                    }
                    if (!sublist.acquired) {
                        if (!sublist.query_future.valid()) {
                            sublist.query_future = std::async(std::launch::async, [=, &database]() { 
                                return database.get_package_list(letter); } );
                        }
                        using namespace std::chrono_literals;
                        auto result = sublist.query_future.wait_for(0s);
                        if (result == std::future_status::timeout) {
                            ImGui::TextUnformatted("(Querying...)");
                        }
                        else if (result == std::future_status::ready) {
                            sublist.packages = sublist.query_future.get();
                            sublist.acquired = true;
                        }
                    }
                    if (sublist.acquired) {
                        if (sublist.packages.empty())
                            ImGui::TextUnformatted("(No packages)");
                        else
                            for (const auto& package: sublist.packages) {
                                ImGui::Selectable(package.name.c_str());
                                ImGui::SameLine();
                                ImGui::TextUnformatted(package.repository.c_str());
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