#include "gomoku/ai_player.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace gomoku::ai {
namespace fs = std::filesystem;

namespace {

char stoneChar(const gomoku::Stone stone) {
    if (stone == gomoku::Stone::BLACK) {
        return 'B';
    }
    if (stone == gomoku::Stone::WHITE) {
        return 'W';
    }
    return '.';
}

std::string quoteArg(const std::string& value) {
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            out += "\\\"";
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

std::string compactMessage(const std::string& text, const size_t max_len = 180) {
    std::string out;
    bool has_space = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!has_space && !out.empty()) {
                out += ' ';
            }
            has_space = true;
            continue;
        }
        out += ch;
        has_space = false;
    }

    if (out.size() <= max_len) {
        return out;
    }
    return out.substr(0, max_len - 3) + "...";
}

fs::path executableDir() {
#if defined(_WIN32)
    std::array<char, 4096> buffer{};
    if (const DWORD size = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())); size > 0 && size < buffer.size()) {
        return fs::path(std::string(buffer.data(), size)).parent_path();
    }
#endif

    std::error_code ec;
    return fs::current_path(ec);
}

std::vector<fs::path> baseDirs(const std::string& source_dir) {
    std::vector<fs::path> paths;
    const auto add = [&paths](fs::path value) {
        std::error_code ec;
        value = fs::absolute(value, ec).lexically_normal();
        const auto key = value.generic_string();
        for (const auto& existing : paths) {
            if (existing.generic_string() == key) {
                return;
            }
        }
        paths.push_back(std::move(value));
    };

    const fs::path exe_dir = executableDir();
    add(source_dir);
    add(exe_dir);
    add(exe_dir.parent_path());
    add(fs::current_path());
    add(fs::current_path().parent_path());
    return paths;
}

std::optional<fs::path> findFile(const std::vector<fs::path>& bases, const std::string& name) {
    for (const auto& base : bases) {
        const auto candidate = (base / name).lexically_normal();
        if (std::error_code ec; fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string searchedPaths(const std::vector<fs::path>& bases, const std::string& name) {
    std::string output;
    for (const auto& base : bases) {
        if (!output.empty()) {
            output += ", ";
        }
        output += (base / name).generic_string();
    }
    return output;
}

std::vector<std::string> pythonCommands(const std::vector<fs::path>& bases, std::string preferred_python) {
    std::vector<std::string> commands;
    std::vector<std::string> seen;

    const auto push = [&commands, &seen](std::string command) {
        if (command.empty()) {
            return;
        }
        if (command.size() >= 2 && command.front() == '"' && command.back() == '"') {
            command = command.substr(1, command.size() - 2);
        }

        std::string key = command;
        for (auto& ch : key) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (std::ranges::find(seen, key) != seen.end()) {
            return;
        }
        seen.push_back(std::move(key));

        if (command.find_first_of("/\\:") != std::string::npos) {
            if (std::error_code ec; !fs::exists(command, ec)) {
                return;
            }
            command = fs::path(command).lexically_normal().generic_string();
        }
        commands.push_back(std::move(command));
    };

    if (const char* env_py = std::getenv("GOMOKU_PYTHON_EXECUTABLE"); env_py && *env_py) {
        push(env_py);
    }
    push(std::move(preferred_python));

    static constexpr std::array kCandidateExecutables = {
        ".gomoku-python/Scripts/python.exe",
        ".gomoku-python/python.exe",
        ".venv/Scripts/python.exe",
        ".venv/bin/python3",
        ".venv/bin/python",
        "Scripts/python.exe",
        "python/bin/python3",
        "python/bin/python"
    };
    for (const auto& base : bases) {
        for (const auto* relative : kCandidateExecutables) {
            push((base / relative).generic_string());
        }
    }

    push("python");
    push("python3");
    return commands;
}

std::string serializeBoard(const gomoku::Board& board) {
    const int size = board.getSize();
    std::string out;
    out.reserve(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            out += stoneChar(board.getStone(x, y));
        }
    }
    return out;
}

std::optional<std::pair<int, int>> parseMove(const std::string& output) {
    std::istringstream ss(output);
    std::string line;
    std::optional<std::pair<int, int>> parsed_move;

    while (std::getline(ss, line)) {
        std::istringstream line_stream(line);
        int x = 0;
        int y = 0;
        if (std::string trailing; (line_stream >> x >> y) && !(line_stream >> trailing)) {
            parsed_move = std::pair{x, y};
        }
    }

    return parsed_move;
}

#if defined(_WIN32)
int runProcess(const std::string& command_line, std::string& output) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = write_pipe;
    startup_info.hStdError = write_pipe;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::vector<char> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back('\0');

    if (!CreateProcessA(nullptr,
                        mutable_command.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startup_info,
                        &process_info)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return -1;
    }

    CloseHandle(write_pipe);

    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
        output.append(buffer.data(), read);
    }

    CloseHandle(read_pipe);
    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    return static_cast<int>(exit_code);
}
#endif

struct RawModelResult {
    std::optional<std::pair<int, int>> move;
    std::string reason;
};

RawModelResult queryModelMove(const gomoku::Board& board,
                              const std::string& source_dir,
                              const std::string& preferred_python) {
    const auto bases = baseDirs(source_dir);

    auto script = findFile(bases, "runModelAndReturnPoint.py");
    if (!script) {
        script = findFile(bases, "python/runModelAndReturnPoint.py");
    }
    if (!script) {
        return {
            std::nullopt,
            "runModelAndReturnPoint.py not found; searched: " + compactMessage(searchedPaths(bases, "runModelAndReturnPoint.py"), 260)
        };
    }

    auto model = findFile(bases, "gomoku_model.pt");
    if (!model) {
        return {
            std::nullopt,
            "gomoku_model.pt not found; searched: " + compactMessage(searchedPaths(bases, "gomoku_model.pt"), 260)
        };
    }

    const auto candidates = pythonCommands(bases, preferred_python);
    if (candidates.empty()) {
        return {std::nullopt, "no usable python command found"};
    }

    const std::string board_text = serializeBoard(board);
    const char current = stoneChar(board.getCurrentPlayer());
    const std::string args =
        "--board-size " + std::to_string(board.getSize()) + " "
        "--model-path " + quoteArg(model->generic_string()) + " "
        "--current " + std::string(1, current) + " "
        "--board " + quoteArg(board_text);

    std::vector<std::string> logs;
    logs.reserve(candidates.size());

    for (const auto& python : candidates) {
        std::string output;
#if defined(_WIN32)
        const std::string command =
            quoteArg(python) + " " + quoteArg(script->generic_string()) + " " + args;
        const int rc = runProcess(command, output);
#else
        const std::string command =
            quoteArg(python) + " " + quoteArg(script->generic_string()) + " " + args + " 2>&1";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            logs.push_back("py=" + python + " -> cannot spawn");
            continue;
        }
        std::array<char, 256> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
            output += buffer.data();
        }
        const int rc = pclose(pipe);
#endif

        if (rc != 0) {
            logs.push_back("py=" + python + " exit=" + std::to_string(rc) + ": " + compactMessage(output));
            continue;
        }

        const auto parsed = parseMove(output);
        if (!parsed) {
            return {
                std::nullopt,
                "cannot parse AI move (py=" + python + "): " + compactMessage(output)
            };
        }

        const auto [x, y] = *parsed;
        if (const int size = board.getSize(); x < 0 || x >= size || y < 0 || y >= size) {
            return {
                std::nullopt,
                "AI move out of range: (" + std::to_string(x) + "," + std::to_string(y) + ")"
            };
        }

        if (board.getStone(x, y) != gomoku::Stone::EMPTY) {
            return {
                std::nullopt,
                "AI move is not empty: (" + std::to_string(x) + "," + std::to_string(y) + ")"
            };
        }

        return {std::pair{x, y}, ""};
    }

    std::string summary;
    for (const auto& entry : logs) {
        if (!summary.empty()) {
            summary += " | ";
        }
        summary += entry;
    }

    const fs::path debug_log_path = fs::path(source_dir) / "ai_debug.log";
    if (FILE* file = fopen(debug_log_path.string().c_str(), "w")) {
        fprintf(file, "%s\n", summary.c_str());
        fclose(file);
    }

    return {
        std::nullopt,
        "all python candidates failed: " + compactMessage(summary, 1200)
    };
}

std::optional<std::pair<int, int>> fallbackMove(const gomoku::Board& board) {
    const int size = board.getSize();
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (board.getStone(x, y) == gomoku::Stone::EMPTY) {
                return std::pair{x, y};
            }
        }
    }
    return std::nullopt;
}

} // namespace

Player::Player(std::string source_dir, std::string preferred_python)
    : source_dir_(std::move(source_dir)),
      preferred_python_(std::move(preferred_python)) {}

MoveResult Player::makeMove(const Board& board) const {
    auto [model_move, reason] = queryModelMove(board, source_dir_, preferred_python_);
    if (model_move) {
        return {.move = model_move, .used_fallback = false, .diagnostic = ""};
    }

    const auto fallback = fallbackMove(board);
    if (!fallback) {
        if (reason.empty()) {
            reason = "no legal fallback move";
        }
        return {.move = std::nullopt, .used_fallback = true, .diagnostic = std::move(reason)};
    }

    return {.move = fallback, .used_fallback = true, .diagnostic = std::move(reason)};
}

} // namespace gomoku::ai
