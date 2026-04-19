/**
* @file ui_controller.h
 * @author shawn
 * @date 2026/3/22
 * @brief Terminal UI controller for Gomoku.
 */
#ifndef GOMOKU_UI_CONTROLLER_H
#define GOMOKU_UI_CONTROLLER_H

#include "../core/game_session.h"
#include <memory>

namespace UI {
    class Controller {
    public:
        explicit Controller(gomoku::GameSession& session);
        ~Controller();
        Controller(const Controller&) = delete;
        Controller& operator=(const Controller&) = delete;

        void Start() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
