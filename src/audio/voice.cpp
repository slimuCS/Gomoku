/**
 * @file voice.cpp
 * @author shawn
 * @date 2026/3/31
 * @brief 
 *
 * * Under the hood:
 * - Memory Layout: 
 * - System Calls / Interactions: 
 * - Resource Impact: 
 */
#include "../../include/gomoku/audio/voice.h"

#define MINIAUDIO_IMPLEMENTATION
#include "gomoku/miniaudio.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static ma_engine engine;
static ma_sound g_bgm;
static bool g_isInitalized = false;
static bool g_hasBgm = false;

namespace {

fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#else
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}

std::vector<fs::path> assetRoots() {
    const fs::path exe_dir = executableDir();

    std::vector<fs::path> roots = {
        exe_dir / "assets" / "audio",
        exe_dir.parent_path() / "assets" / "audio",
        exe_dir.parent_path().parent_path() / "assets" / "audio",
        fs::current_path() / "assets" / "audio",
    };

#ifdef GOMOKU_SOURCE_DIR
    roots.push_back(fs::path(GOMOKU_SOURCE_DIR) / "assets" / "audio");
#endif

    return roots;
}

std::optional<std::string> assetPath(const char* filename) {
    for (const auto& root : assetRoots()) {
        const fs::path candidate = (root / filename).lexically_normal();
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            return candidate.string();
        }
    }
    return std::nullopt;
}

void logAudioAssetFailureOnce(const std::string& key, const std::string& detail) {
    static std::unordered_set<std::string> reported_failures;
    if (reported_failures.insert(key).second) {
        std::cerr << detail << std::endl;
    }
}

bool playAudioFile(const char* filename) {
    if (!g_isInitalized) {
        return false;
    }

    const auto path = assetPath(filename);
    if (!path.has_value()) {
        logAudioAssetFailureOnce(
            filename,
            "Audio asset not found: " + std::string(filename) + ". Checked executable, working directory, and source tree asset folders."
        );
        return false;
    }

    if (const ma_result result = ma_engine_play_sound(&engine, path->c_str(), nullptr); result != MA_SUCCESS) {
        logAudioAssetFailureOnce(
            std::string("play:") + filename,
            "Failed to play audio asset " + std::string(filename) + ": miniaudio error " + std::to_string(result)
        );
        return false;
    }

    return true;
}

} // namespace

voice gameVoice;

bool voice::initAudioSystem() {
    if (g_isInitalized) {
        return true;
    }

    ma_result result = ma_engine_init(nullptr, &engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine: " << result << std::endl;
        return false;
    }

    const auto bgm_path = assetPath("backGround.mp3");
    if (!bgm_path.has_value()) {
        logAudioAssetFailureOnce(
            "backGround.mp3",
            "Audio asset not found: backGround.mp3. Background music will be disabled."
        );
        g_isInitalized = true;
        return true;
    }

    result = ma_sound_init_from_file(&engine, bgm_path->c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &g_bgm);

    g_hasBgm = (result == MA_SUCCESS);
    if (g_hasBgm) {
        ma_sound_set_looping(&g_bgm, true);
    } else {
        logAudioAssetFailureOnce(
            "bgm_init",
            "Failed to initialize background music: miniaudio error " + std::to_string(result)
        );
    }
    g_isInitalized = true;
    return true;
}

void voice::cleanupAudioSystem() {
    if (g_isInitalized) {
        if (g_hasBgm) {
            ma_sound_uninit(&g_bgm);
            g_hasBgm = false;
        }
        ma_engine_uninit(&engine);
        g_isInitalized = false;
    }
}

void voice::clickSound() {
    playAudioFile("click.mp3");
}

void voice::backGroundMusic() {
    if (!g_isInitalized || !g_hasBgm) return;
    ma_sound_start(&g_bgm);
}

void voice::placeStoneSound() {
    playAudioFile("placeStoneVoice.mp3");
}

void voice::victorySound() {
    playAudioFile("victory.mp3");
}

void voice::defeatSound() {
    playAudioFile("defeat.mp3");
}

void voice::stopBackgroundMusic() {
    if (!g_isInitalized || !g_hasBgm) return;
    ma_sound_stop(&g_bgm);
}


