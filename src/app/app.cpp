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

#include "gomoku/game_session.h"
#include "gomoku/ui_controller.h"
#include "gomoku/voice.h"

int main() {
    gomoku::GameSession session(15);
    const UI::Controller controller(session);

    gameVoice.initAudioSystem();
    gameVoice.backGroundMusic();

    controller.Start();

    gameVoice.cleanupAudioSystem();
    return 0;
}
