#ifndef GOMOKU_AI_PLAYER_H
#define GOMOKU_AI_PLAYER_H

#include "engine.h"

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
    Player(std::string source_dir, std::string preferred_python);

    [[nodiscard]] MoveResult makeMove(const Board& board) const;

private:
    std::string source_dir_;
    std::string preferred_python_;
};

} // namespace gomoku::ai

#endif
