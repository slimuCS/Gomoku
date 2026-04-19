#include "gomoku/ai_player.h"
#include "gomoku/engine.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void placeSequence(gomoku::Board& board,
                   const std::initializer_list<std::pair<int, int>> moves) {
    for (const auto& [x, y] : moves) {
        require(board.placeStone(x, y), "failed to place a setup move");
    }
}

bool isOneOf(const std::pair<int, int>& move,
             const std::pair<int, int>& first,
             const std::pair<int, int>& second) {
    return move == first || move == second;
}

} // namespace

int main() {
    gomoku::ai::Player ai;

    {
        gomoku::Board board(15);
        const auto result = ai.makeMove(board);
        require(result.move.has_value(), "AI should produce an opening move");
        require(*result.move == std::pair<int, int>{7, 7}, "AI should open at the center on an empty board");
    }

    {
        gomoku::Board board(15);
        placeSequence(board, {
            {0, 0}, {7, 7},
            {0, 1}, {8, 7},
            {0, 2}, {9, 7},
            {0, 3}, {10, 7},
            {0, 4}
        });

        const auto result = ai.makeMove(board);
        require(result.move.has_value(), "AI should choose a winning move when one exists");
        require(isOneOf(*result.move, {6, 7}, {11, 7}),
                "AI should finish its own open four before considering other rewards");
    }

    {
        gomoku::Board board(15);
        placeSequence(board, {
            {7, 7}, {0, 0},
            {8, 7}, {0, 1},
            {9, 7}, {0, 2},
            {10, 7}
        });

        const auto result = ai.makeMove(board);
        require(result.move.has_value(), "AI should choose a blocking move against an immediate loss");
        require(isOneOf(*result.move, {6, 7}, {11, 7}),
                "AI should block the opponent's open four");
    }

    return 0;
}
