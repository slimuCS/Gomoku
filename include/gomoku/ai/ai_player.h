#ifndef GOMOKU_AI_PLAYER_H
#define GOMOKU_AI_PLAYER_H

#include "../core/engine.h"

#include <optional>
#include <string>
#include <utility>

namespace gomoku::ai {

struct MoveResult {
    std::optional<std::pair<int, int>> move;
    bool used_fallback = false;
    std::string diagnostic;
};

class Player {
public:
    Player() = default;

    [[nodiscard]] MoveResult makeMove(const Board& board) const;
};

} // namespace gomoku::ai

#endif
