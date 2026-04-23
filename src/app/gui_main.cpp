#include "../../include/gomoku/gui/http_server.h"

int main() {
    gomoku::gui::HttpServer server;
    server.run(7777);
    return 0;
}
