# GreatTamanaEngine

A raw game engine built from scratch.

## Goal

The plan is to develop a raw game engine from scratch, with the very foundation
built on **SDL3** (the new generation after SDL2) for window and event handling.

## Architecture

The very first architecture layers the engine like this:

```
SDL -> Application -> Window and Renderer -> Game
```

- **Application** is the only layer that knows about SDL directly. It owns the
  main loop and is responsible for initializing/shutting down SDL.
- **Window** and **Renderer** are custom objects that act as an abstraction
  layer on top of SDL. Other layers (like Game) interact with these custom
  objects instead of touching SDL directly.
- **Game** sits on top of Window and Renderer, and has no direct knowledge of
  SDL either.

At this stage, Window and Renderer will still internally depend on SDL
objects — that's okay for now. The abstraction can be tightened later as the
engine evolves.

## Building

Windows only, for now.

Prerequisites:

- **CMake 3.19+**
- **A C++20 toolchain CMake can generate for**, e.g. one of:
  - Visual Studio 2017+ (with the "Desktop development with C++" workload)
  - Ninja + MSVC/clang
  - MinGW-w64 (g++) + mingw32-make or Ninja
- **Internet access the first time you configure.** `SDL3` is not vendored in
  this repo — CMake downloads the official prebuilt SDL3 headers/DLL/import
  lib straight from the SDL GitHub releases
  (https://github.com/libsdl-org/SDL) into `include/`, `SDL3.dll`, and
  `lib/SDL3.lib` on first configure (see `cmake/FetchSDL3.cmake`). These are
  gitignored — every clone fetches its own copy, so nothing SDL-related is
  committed to the repo. Subsequent configures reuse what was already
  downloaded and don't need the network again.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
build\Debug\GreatTamanaEngine.exe
```

(swap the `-G` generator for whatever matches your installed toolchain, e.g.
`-G Ninja` or `-G "MinGW Makefiles"`)

## Status

Early foundation stage. Architecture and abstractions are still being
established.
