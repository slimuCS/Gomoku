/**
 * @file app.cpp
 * @author shawn
 * @date 2026/3/19
 * @brief the file is to play the gomoku game
 *
 * * Under the hood:
 * - Memory Layout:
 * - System Calls / Interactions:
 * - Resource Impact:
 */

#include "../../include/gomoku/core/game_session.h"
#include "../../include/gomoku/ui/ui_controller.h"
#include "../../include/gomoku/audio/voice.h"

int main() {
    gomoku::GameSession session(15);
    const UI::Controller controller(session);

    voice::initAudioSystem();
    voice::backGroundMusic();

    controller.Start();

    voice::cleanupAudioSystem();
    return 0;
}
