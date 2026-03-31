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
#include "gomoku/voice.h"

#define MINIAUDIO_IMPLEMENTATION
#include "gomoku/miniaudio.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

#ifdef GOMOKU_SOURCE_DIR
constexpr auto kSourceDir = GOMOKU_SOURCE_DIR;
#else
constexpr const char* kSourceDir = ".";
#endif

namespace {

std::string assetPath(const char* filename) {
    return (fs::path(kSourceDir) / "assets" / "audio" / filename).lexically_normal().string();
}

} // namespace

static ma_engine engine;
static ma_sound g_bgm;
static bool g_isInitalized = false;
static bool g_hasBgm = false;

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
    result = ma_sound_init_from_file(&engine, bgm_path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &g_bgm);

    g_hasBgm = (result == MA_SUCCESS);
    if (g_hasBgm) {
        ma_sound_set_looping(&g_bgm, true);
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
    if (!g_isInitalized) return;
    const auto click_path = assetPath("click.mp3");
    ma_engine_play_sound(&engine, click_path.c_str(), nullptr);
}

void voice::backGroundMusic() {
    if (!g_isInitalized || !g_hasBgm) return;
    ma_sound_start(&g_bgm);
}

void voice::placeStoneSound() {
    if (!g_isInitalized) return;
    const auto place_stone_path = assetPath("placeStoneVoice.mp3");
    ma_engine_play_sound(&engine, place_stone_path.c_str(), nullptr);
}

void voice::stopBackgroundMusic() {
    if (!g_isInitalized || !g_hasBgm) return;
    ma_sound_stop(&g_bgm);
}


