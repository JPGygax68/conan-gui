cmake_minimum_required(VERSION 3.16)

include(pmm.cmake)

project(Conan-GUI_root)

pmm(
  CONAN BINCRAFTERS COMMUNITY
)

find_package(sdl2 CONFIG REQUIRED)

add_subdirectory(src)
