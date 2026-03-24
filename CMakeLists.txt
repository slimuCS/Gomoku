cmake_minimum_required(VERSION 3.20)
project(Gomoku LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
        GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

add_executable(Gomoku_Project
        include/gomoku/engine.h
        src/engine.cpp
        src/app.cpp
        src/ui_controller.cpp
)

target_include_directories(Gomoku_Project PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Gomoku_Project PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
)
