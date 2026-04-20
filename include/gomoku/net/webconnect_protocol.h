/**
 * @file webconnect_protocol.h
 * @brief Internal protocol helpers shared by the transport and regression tests.
 */
#ifndef GOMOKU_WEBCONNECT_PROTOCOL_H
#define GOMOKU_WEBCONNECT_PROTOCOL_H

#include "../core/game_session.h"

#include <string>

namespace gomoku::net {

[[nodiscard]] bool applyRemoteMove(GameSession& session,
                                   Stone remote_stone,
                                   int x,
                                   int y,
                                   std::string& error);

} // namespace gomoku::net

#endif // GOMOKU_WEBCONNECT_PROTOCOL_H
