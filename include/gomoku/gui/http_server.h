#pragma once
#include "../core/game_session.h"
#include <cstdint>
#include <memory>

namespace gomoku::gui {

class HttpServer {
public:
    HttpServer();
    void run(std::uint16_t port = 7777);

private:
    std::unique_ptr<GameSession> session_;
    int pending_board_size_ = 15;  // applied on next New Game

    std::string stateJson() const;
    std::string handleRequest(const std::string& method,
                              const std::string& path,
                              const std::string& body);
    static std::string htmlPage();
    static std::string response200(const std::string& content_type,
                                   const std::string& body);
    static std::string response400(const std::string& msg);
};

} // namespace gomoku::gui
