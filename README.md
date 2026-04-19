# Gomoku

This repository currently ships a native C++20 terminal Gomoku game built with
FTXUI. The maintained product surface is the C++ application plus C++
regression tests.

This repo does not currently ship:

- Python bindings
- a `uv` workflow
- a pip-installable inference package
- model-checkpoint-driven Python inference scripts

## Features

- Local PvP
- Local PvE with the built-in C++ reward-engine AI
- Remote PvP over TCP (`Host Remote Game` / `Join Remote Game`)
- Save and load using move-history replay
- Optional audio playback from `assets/audio/`
- Regression coverage for remote turn validation and save/load integrity

## Requirements

- CMake `>= 3.20`
- A C++20 compiler
  - GCC 10+
  - Clang 12+
  - MSVC 2019+
- Network access during the first CMake configure, because FTXUI is fetched via
  `FetchContent`

## Build

Configure:

```bash
cmake -S . -B build
```

Build:

```bash
cmake --build build
```

For Visual Studio multi-config generators:

```bash
cmake --build build --config Release
```

## Run

Windows with a multi-config generator:

```bash
build\Release\Gomoku_Project.exe
```

Windows with Ninja or MinGW:

```bash
build\Gomoku_Project.exe
```

macOS / Linux:

```bash
./build/Gomoku_Project
```

## Test

Build the regression test target:

```bash
cmake --build build --target gomoku_regression_tests
```

Run the regression tests:

```bash
ctest --test-dir build -R gomoku_regression_tests --output-on-failure
```

## Audio Assets

The application expects these files under `assets/audio/`:

- `backGround.mp3`
- `click.mp3`
- `placeStoneVoice.mp3`

## Project Layout

```text
Gomoku/
|-- CMakeLists.txt
|-- README.md
|-- docs/
|   `-- scoring.md
|-- include/gomoku/
|   |-- ai/
|   |-- audio/
|   |-- core/
|   |-- net/
|   `-- ui/
|-- src/
|   |-- ai/
|   |   `-- ai_player.cpp
|   |-- app/
|   |   `-- app.cpp
|   |-- audio/
|   |   `-- voice.cpp
|   |-- core/
|   |   |-- engine.cpp
|   |   `-- game_session.cpp
|   |-- net/
|   |   `-- webConnect.cpp
|   `-- ui/
|       `-- ui_controller.cpp
|-- assets/
|   `-- audio/
`-- tests/
    `-- regression_tests.cpp
```

## Scope

If the project later reintroduces Python bindings or a Python distribution path,
that workflow should come back together with a real build target, install
source, and tested documentation. Until then, the canonical entry points are the
C++ executable and the C++ regression tests above.
