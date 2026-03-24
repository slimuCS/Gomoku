/**
 * @file bindings.cpp
 * @author shawn
 * @date 2026/3/24
 * @brief 
 *
 * * Under the hood:
 * - Memory Layout: 
 * - System Calls / Interactions: 
 * - Resource Impact: 
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "gomoku/engine.h"

namespace py = pybind11;

PYBIND11_MODULE(gomoku_ai, m) {
    m.doc() = "Gomoku Core Engine built with C++ and pybind11";

    py::enum_<gomoku::Stone>(m, "Stone")
        .value("EMPTY", gomoku::Stone::EMPTY)
        .value("BLACK", gomoku::Stone::BLACK)
        .value("WHITE", gomoku::Stone::WHITE)
        .export_values();

    py::enum_<gomoku::GameStatus>(m, "GameStatus")
        .value("PLAYING", gomoku::GameStatus::PLAYING)
        .value("BLACK_WIN", gomoku::GameStatus::BLACK_WIN)
        .value("WHITE_WIN", gomoku::GameStatus::WHITE_WIN)
        .value("DRAW", gomoku::GameStatus::DRAW)
        .export_values();

    py::class_<gomoku::Board>(m, "Board")
        .def(py::init<int>(), py::arg("size") = 15)
        .def("place_stone", &gomoku::Board::placeStone)
        .def("get_current_player", &gomoku::Board::getCurrentPlayer)
        .def("get_status", &gomoku::Board::getStatus)
        .def("get_stone", &gomoku::Board::getStone)
        .def("get_observation", [](const gomoku::Board &b) {
            const int size = b.getSize();

            py::array_t<float> observation({3, size, size});
            auto r = observation.mutable_unchecked<3>();

            // me is your opponent, opponent is you, because the observation is from the perspective of the current player
            const gomoku::Stone me = b.getCurrentPlayer();
            const gomoku::Stone opponent = (me == gomoku::Stone::BLACK) ? gomoku::Stone::WHITE : gomoku::Stone::BLACK;

            for (int x = 0; x < size; ++x) {
                for (int y = 0; y < size; ++y) {
                    const gomoku::Stone stone = b.getStone(x, y);
                    r(0, x, y) = (stone == me) ? 1.0f : 0.0f; // My stones
                    r(1, x, y) = (stone == opponent) ? 1.0f : 0.0f; // Opponent's stones
                    r(2, x, y) = (stone == gomoku::Stone::EMPTY) ? 1.0f : 0.0f; // Empty cells
                }
             }

             return observation;
        });
}