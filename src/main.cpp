#include <iostream>
#include <cstdio>
#include <map>
#include <imgui.h>
#include "./database.h"
#include "./repo_reader.h"
#include "./imgui_app.h"


int main(int, char **)
{
    Conan::Database database;
    Conan::Repository_reader repo_reader{ database };

    auto initial_requeue = repo_reader.requeue_all();

    auto package_list = database.get_package_list();

    imgui_init();

    (void) initial_requeue.get();

    while (imgui_continue()) {
    
        imgui_new_frame();

        if (ImGui::Begin("Conan")) {
            for (const auto& pkg: package_list) {
                ImGui::Selectable(pkg.c_str());
            }
        }
        ImGui::End();

        imgui_frame_done();
    }

    return 0;
}