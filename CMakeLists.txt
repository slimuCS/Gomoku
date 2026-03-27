cmake_minimum_required(VERSION 3.20)
project(Gomoku LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 3.13 COMPONENTS Interpreter Development REQUIRED)
file(TO_CMAKE_PATH "${Python3_EXECUTABLE}" PYTHON_EXECUTABLE_FOR_CPP)
set(PYBIND11_FINDPYTHON ON)

include(FetchContent)

execute_process(
        COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
        OUTPUT_VARIABLE pybind11_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE PYBIND11_STATUS
        ERROR_QUIET
)

if(NOT PYBIND11_STATUS EQUAL 0 OR pybind11_DIR STREQUAL "")
    find_program(PYTHON_FALLBACK_EXECUTABLE NAMES python python3)
    if(PYTHON_FALLBACK_EXECUTABLE)
        execute_process(
                COMMAND "${PYTHON_FALLBACK_EXECUTABLE}" -m pybind11 --cmakedir
                OUTPUT_VARIABLE pybind11_DIR_FALLBACK
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE PYBIND11_FALLBACK_STATUS
                ERROR_QUIET
        )
        if(PYBIND11_FALLBACK_STATUS EQUAL 0 AND NOT pybind11_DIR_FALLBACK STREQUAL "")
            set(pybind11_DIR "${pybind11_DIR_FALLBACK}")
        endif()
    endif()
endif()

if(pybind11_DIR STREQUAL "")
    get_filename_component(PYTHON3_INSTALL_ROOT "${Python3_INCLUDE_DIRS}" DIRECTORY)
    set(PYBIND11_FROM_PYTHON3_DIR "${PYTHON3_INSTALL_ROOT}/Lib/site-packages/pybind11/share/cmake/pybind11")
    if(EXISTS "${PYBIND11_FROM_PYTHON3_DIR}/pybind11Config.cmake")
        set(pybind11_DIR "${PYBIND11_FROM_PYTHON3_DIR}")
    endif()
endif()

if(NOT pybind11_DIR STREQUAL "")
    find_package(pybind11 CONFIG QUIET PATHS "${pybind11_DIR}" NO_DEFAULT_PATH)
endif()

if(NOT TARGET pybind11::module)
    find_package(pybind11 CONFIG QUIET)
endif()

if(NOT TARGET pybind11::module)
    message(STATUS "pybind11 was not found in Python/site packages. Fetching pybind11 from GitHub...")
    FetchContent_Declare(pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.13.6
    )
    FetchContent_MakeAvailable(pybind11)
endif()

if(NOT TARGET pybind11::module)
    message(FATAL_ERROR "pybind11::module is unavailable after discovery/fetch. Please install pybind11 in the active Python environment.")
endif()

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

if(COMMAND pybind11_add_module)
    pybind11_add_module(gomoku_ai src/bindings.cpp)
else()
    Python3_add_library(gomoku_ai MODULE src/bindings.cpp)
    target_link_libraries(gomoku_ai PRIVATE pybind11::headers)
    set_target_properties(gomoku_ai PROPERTIES PREFIX "")
endif()

target_link_libraries(gomoku_ai PRIVATE gomoku_core
        -static-libgcc
        -static-libstdc++
        -static
        -lpthread
)

set_target_properties(gomoku_ai PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

target_include_directories(Gomoku_Project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(Gomoku_Project PRIVATE
        GOMOKU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        GOMOKU_PYTHON_EXECUTABLE="${PYTHON_EXECUTABLE_FOR_CPP}"
)

target_link_libraries(Gomoku_Project PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        pybind11::module
        gomoku_core
)
