cmake_minimum_required(VERSION 3.16)

project(conan-gui)

add_executable(
  ${PROJECT_NAME}

  main.cpp

  imgui/imgui.cpp imgui/imgui.h
  imgui/imgui_draw.cpp imgui/imgui_demo.cpp imgui/imgui_widgets.cpp
  imgui/backends/imgui_impl_sdl.cpp imgui/backends/imgui_impl_vulkan.cpp

  imgui_app.h imgui_app_vulkan.cpp

  database.cpp database.h
  repo_reader.cpp repo_reader.h
  async_data.h
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17 cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE imgui imgui/backends)

find_package(Vulkan REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
    CONAN_PKG::sdl2
    CONAN_PKG::fmt
    CONAN_PKG::sqlite3
    Vulkan::Vulkan
)

if (DEFINED MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE Shcore.lib)
endif()