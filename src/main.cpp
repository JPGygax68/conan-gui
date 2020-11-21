#include <iostream>
#include <cstdio>
#include <imgui.h>
#include "./database.h"
#include "./repo_reader.h"
#include "./imgui_app.h"


int main(int, char **)
{
    Conan::Database database;
    Conan::Repository_reader repo_reader{ database };

    auto initial_requeue = repo_reader.requeue_all();

    imgui_init();

    while (imgui_continue()) {
        imgui_new_frame();

        // ConanWindow();

        imgui_frame_done();
    }

    return 0;
}