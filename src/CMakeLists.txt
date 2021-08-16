cmake_minimum_required(VERSION 3.16)

project(conan-gui)

add_executable(
  ${PROJECT_NAME}

  main.cpp

  imgui/imgui.cpp imgui/imgui.h
  imgui/imgui_draw.cpp imgui/imgui_demo.cpp imgui/imgui_widgets.cpp imgui/imgui_tables.cpp
  imgui/backends/imgui_impl_sdl.cpp imgui/backends/imgui_impl_vulkan.cpp

  imgui_app.h imgui_app_vulkan.cpp

  gui_elements.h gui_elements.cpp

  types.h

  repo_reader.cpp repo_reader.h

  alphabetic_tree.cpp alphabetic_tree.h

  cache_db.cpp cache_db.h

  job_queue.h

  sqlite_wrapper/database.cpp sqlite_wrapper/database.h

  async_data.h
  string_utils.h string_utils.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17 cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE imgui imgui/backends)

find_package(Vulkan REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SQLite3 CONFIG REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
    SDL2::SDL2
    # CONAN_PKG::fmt
    SQLite::SQLite
    Vulkan::Vulkan
)

if (DEFINED MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE Shcore.lib)
  target_link_options(${PROJECT_NAME} PRIVATE "/ignore:4099")
endif()