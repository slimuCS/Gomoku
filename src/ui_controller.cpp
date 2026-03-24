#include "gomoku/ui_controller.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"  // for Component, Components
#include "ftxui/component/component_options.hpp"  // for ButtonOption, CheckboxOption, MenuOption
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"  // for Element
#include "ftxui/util/ref.hpp"
#include <vector>
#include <string>

namespace UI {
    using namespace ftxui;

    class InteractiveBoard : public ComponentBase {
    public:
        std::function<Element()> renderLogic;
        std::function<bool(Event)> eventLogic;

        Element Render() override {
            return renderLogic();
        }

        [[nodiscard]] bool Focusable() const override {
            return true;
        }

        bool OnEvent(const Event event) override {
            return eventLogic(event);
        }

    };

    Controller::Controller(gomoku::Board& board) : board(board) {};

    void Controller::Start() {
        const auto container = Container::Tab({
            this->RenderFrontPage(),
            this->RenderGameBoard(),
            this->RenderEndPage()
        }, &active_index);

        this->screen.Loop(container);
    };

    Component Controller::RenderFrontPage() {

        const auto menu = Menu(&menu_entries, &menu_selected);

        auto component = Renderer(menu, [menu] {
           return vbox({
                text("===Gomoku===") | hcenter | bold | color(Color::Cyan),
               separator(),
               menu->Render() | hcenter,
               separator(),
               text("Use arrwo keys to navigate, enter to select") | dim | hcenter
           }) | border | center;
        });

        component |= CatchEvent([this](const Event& event) -> bool {
            if (event == Event::Return) {
                if (this->menu_selected == 0) {
                    this->active_index = 1;
                    return true;
                }
                else if (this->menu_selected == 1) {
                    this->screen.Exit();
                    return true;
                }
            }
            return false;
        });

        return component;
    };
    Component Controller::RenderGameBoard() {
        auto comp = std::make_shared<InteractiveBoard>();

        comp->renderLogic = [this]() -> Element {
            return RenderGrid();
        };

        comp->eventLogic = [this](const Event& event) -> bool {

            if (event == Event::ArrowUp) {
                this->current_y = std::max(0, this->current_y - 1);
                return  true;
            }
            if (event == Event::ArrowDown) {
                this->current_y = std::min(14, this->current_y + 1);
                return true;
            }
            if (event == Event::ArrowLeft) {
                this->current_x = std::max(0, this->current_x - 1);
                return true;
            }
            if (event == Event::ArrowRight) {
                this->current_x = std::min(14, this->current_x + 1);
                return true;
            }


            if (event == Event::Return || event == Event::Character(' ')) {
                if (this->board.getStone(this->current_x, this->current_y) == gomoku::Stone::EMPTY) {
                    if (this->board.placeStone(current_x, current_y, current_player)) {
                        if (gomoku::GameEngine::checkWin(this->board, current_x, current_y)) {
                            this->active_index = 2;
                        }
                        else
                            current_player = (current_player == gomoku::Stone::BLACK) ? gomoku::Stone::WHITE : gomoku::Stone::BLACK;
                    }
                    return true;
                }
            }
            return false;
        };

        return comp;
    };

    Component Controller::RenderEndPage() {
        auto container = Container::Vertical({
            Button("回首頁", [this] {
                this->active_index = 0;
                this->board = gomoku::Board(15);
                this->current_player = gomoku::Stone::BLACK;
            }),
            Button("在玩一局", [this] {
                this->active_index = 1;
                this->board = gomoku::Board(15);
                this->current_player = gomoku::Stone::BLACK;
                this->current_x = 7;
                this->current_y = 7;
            })
        });

        return Renderer(container, [container, this] {
            const std::string winner = (current_player == gomoku::Stone::BLACK) ? "Black Win!" : "White win!";
            return vbox({
                text("Game over") | hcenter | bold | color(Color::Red),
                separator(),
                text("Winner: " + winner) | hcenter | color(Color::Yellow),
                separator(),
                container->Render() | hcenter
            }) | border | center;
        });
    };

    Element Controller::RenderGrid() const {
        const std::string turnText = (current_player == gomoku::Stone::BLACK) ? "目前回合:黑子(○)" : "目前回合:白子(●)";
        const Color turnColor = (current_player == gomoku::Stone::BLACK) ? Color::Red : Color::White;

        auto statusBar = text(turnText) | bold | color(turnColor) | hcenter;
        Elements rows;
        for (int y = 0; y < 15; ++y) {
            Elements cols;
            for (int x = 0; x < 15; ++x) {
                const auto stone = board.getStone(x, y);
                const std::string cell = (board.getStone(x, y) == gomoku::Stone::EMPTY) ? " + " :
                                   (board.getStone(x, y) == gomoku::Stone::BLACK) ? " ○ " : " ● ";
                auto element = text(cell);
                if (x == current_x && y == current_y)
                    element |= bgcolor(Color::Blue) | color(Color::White);
                else if (stone == gomoku::Stone::BLACK) {
                    element |= color(Color::Red);
                }
                else if (stone == gomoku::Stone::WHITE) {
                    //element |= color(Color::Green);
                }
                cols.push_back(element);
            }
            rows.push_back(hbox(std::move(cols)));
        }
        auto boardUI = vbox(std::move(rows)) | hcenter;

        return vbox({
            statusBar,
            separator(),
            boardUI
        }) | border | center ;
    };
}