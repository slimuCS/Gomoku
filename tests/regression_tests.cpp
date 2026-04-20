#include "gomoku/core/game_session.h"
#include "gomoku/net/webconnect_protocol.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {

using gomoku::GameSession;
using gomoku::SessionMode;
using gomoku::Stone;

bool expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool test_remote_move_rejects_out_of_turn_packets() {
    GameSession session(15);
    session.start(SessionMode::PVP);

    std::string error;
    if (!expect(!gomoku::net::applyRemoteMove(session, Stone::WHITE, 7, 7, error),
                "remote WHITE move should be rejected while BLACK is to play")) {
        return false;
    }
    if (!expect(error.find("not the remote player's turn") != std::string::npos,
                "out-of-turn remote move should report turn validation failure")) {
        return false;
    }
    if (!expect(session.move_history().empty(), "rejected remote move must not mutate the session")) {
        return false;
    }

    if (!expect(session.human_move(0, 0, false), "local opening move should succeed")) {
        return false;
    }

    error.clear();
    if (!expect(gomoku::net::applyRemoteMove(session, Stone::WHITE, 1, 1, error),
                "remote WHITE move should be accepted on WHITE's turn")) {
        return false;
    }
    if (!expect(session.move_history().size() == 2, "accepted remote move should be recorded")) {
        return false;
    }
    return expect(session.board().getCurrentPlayer() == Stone::BLACK,
                  "turn should advance back to BLACK after a valid remote WHITE move");
}

bool test_deserialize_rejects_invalid_board_size_without_mutating_state() {
    namespace fs = std::filesystem;

    const fs::path temp_dir = fs::temp_directory_path() / "gomoku_regression_tests";
    fs::create_directories(temp_dir);
    const fs::path save_path = temp_dir / "invalid_size.gomoku";

    {
        std::ofstream save(save_path);
        save << "mode PVP\n";
        save << "size 0\n";
    }

    GameSession session(15);
    session.start(SessionMode::PVP);
    if (!expect(session.human_move(3, 3, false), "baseline move should succeed")) {
        return false;
    }

    if (!expect(!session.deserialize(save_path.string()), "deserialize should reject board size 0")) {
        return false;
    }
    if (!expect(session.last_persistence_error().find("supported range") != std::string::npos,
                "invalid size should surface a descriptive error")) {
        return false;
    }
    if (!expect(session.board().getSize() == 15, "failed deserialize must preserve the existing board size")) {
        return false;
    }
    return expect(session.move_history().size() == 1,
                  "failed deserialize must preserve the existing move history");
}

bool test_deserialize_rejects_partial_replay_without_mutating_state() {
    namespace fs = std::filesystem;

    const fs::path temp_dir = fs::temp_directory_path() / "gomoku_regression_tests";
    fs::create_directories(temp_dir);
    const fs::path save_path = temp_dir / "partial_replay.gomoku";

    {
        std::ofstream save(save_path);
        save << "mode PVP\n";
        save << "size 15\n";
        save << "7 7\n";
        save << "7 7\n";
    }

    GameSession session(15);
    session.start(SessionMode::PVP);
    if (!expect(session.human_move(2, 2, false), "baseline move should succeed")) {
        return false;
    }

    if (!expect(!session.deserialize(save_path.string()), "deserialize should reject an invalid replay")) {
        return false;
    }
    if (!expect(session.last_persistence_error().find("Illegal move") != std::string::npos,
                "invalid replay should report the illegal move")) {
        return false;
    }
    if (!expect(session.move_history().size() == 1, "failed deserialize must not partially replace the move history")) {
        return false;
    }
    return expect(session.last_move() == std::optional<std::pair<int, int>>{{2, 2}},
                  "failed deserialize must preserve the last move");
}

} // namespace

int main() {
    bool ok = true;
    ok = test_remote_move_rejects_out_of_turn_packets() && ok;
    ok = test_deserialize_rejects_invalid_board_size_without_mutating_state() && ok;
    ok = test_deserialize_rejects_partial_replay_without_mutating_state() && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "All regression tests passed.\n";
    return 0;
}
