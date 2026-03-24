cmake_minimum_required(VERSION 3.20)
project(Gomoku LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 3.13 COMPONENTS Interpreter Development REQUIRED)

execute_process(
        COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
        OUTPUT_VARIABLE pybind11_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE PYBIND11_STATUS
)

find_package(pybind11 REQUIRED PATHS ${pybind11_DIR} NO_DEFAULT_PATH)

include(FetchContent)
FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
        GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

add_library(gomoku_core STATIC src/engine.cpp)
target_include_directories(gomoku_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)


add_executable(Gomoku_Project
        src/app.cpp
        src/ui_controller.cpp
)

pybind11_add_module(gomoku_ai src/bindings.cpp)

target_link_libraries(gomoku_ai PRIVATE gomoku_core)

target_include_directories(Gomoku_Project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(Gomoku_Project PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        pybind11::module
        gomoku_core
)