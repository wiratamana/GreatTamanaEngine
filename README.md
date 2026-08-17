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

### Event handling

SDL's raw event stream never reaches `Game` (or anything else past
`Application`) directly. Each frame, `Application::Run()` polls SDL and turns
every event into the engine's own vocabulary before anything else sees it:

```
SDL_Event -> EventTranslator -> gte::Event -> InputState.Apply() + Game::OnEvent()
                                                                 \-> Game::Update(dt, InputState)
```

- **`EventTranslator`** (`src/Application/EventTranslator.h/.cpp`) is the only
  other place besides `Application` itself allowed to touch `SDL_Event`. It
  translates each raw SDL event into `gte::Event` (`src/Event/Event.h`), using
  engine-owned `KeyCode`/`MouseButton` enums instead of SDL's — so nothing
  past this point ever needs to know SDL exists.
- **`InputState`** (`src/Input/InputState.h/.cpp`) tracks continuous,
  queryable input state (held keys/buttons, mouse position/delta), built up
  by applying translated events. It's passed into `Game::Update()` for
  polling-style input, e.g. `input.IsKeyDown(KeyCode::W)` for movement while a
  key is held.
- **`Game::OnEvent(const Event&)`** is called once per translated event, for
  discrete/one-shot reactions (window resized, a key just pressed, quit) —
  as opposed to the continuous polling done via `InputState` in `Update()`.

## Building

Windows only, for now.

Prerequisites:

- **CMake 3.19+**
- **A C++20 toolchain CMake can generate for**, e.g. one of:
  - Visual Studio 2017+ (with the "Desktop development with C++" workload)
  - Ninja + MSVC/clang
  - MinGW-w64 (g++) + mingw32-make or Ninja
- `SDL3` is not vendored in this repo — CMake downloads the official prebuilt
  SDL3 headers/DLL/import lib straight from the SDL GitHub releases
  (https://github.com/libsdl-org/SDL) into `include/`, `SDL3.dll`, and
  `lib/SDL3.lib` on first configure (see `cmake/FetchSDL3.cmake`). These are
  gitignored — every clone fetches its own copy, so nothing SDL-related is
  committed to the repo. Subsequent configures reuse what was already
  downloaded and don't need the network again.
- Vulkan is likewise not vendored, and **no Vulkan SDK installation is
  required**. CMake fetches the official `Vulkan-Headers`
  (https://github.com/KhronosGroup/Vulkan-Headers) into `include/vulkan` and
  `include/vk_video`, plus `volk` (https://github.com/zeux/volk) — a tiny
  meta-loader — into `third_party/volk` (see `cmake/FetchVulkan.cmake`).
  volk resolves all Vulkan function pointers by dynamically loading
  `vulkan-1.dll` at runtime, so there's nothing to link against or copy next
  to the .exe: any machine with a Vulkan-capable GPU driver installed already
  has `vulkan-1.dll` on its normal DLL search path. These are gitignored too,
  same as SDL3.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
build\Debug\GreatTamanaEngine.exe
```

(swap the `-G` generator for whatever matches your installed toolchain, e.g.
`-G Ninja` or `-G "MinGW Makefiles"`)

## Status

Early foundation stage. Architecture and abstractions are still being
established. Window/Renderer/Game scaffolding is in place, and event handling
now flows through `EventTranslator`/`InputState` as described above instead of
raw SDL events reaching `Game` directly. Vulkan-Headers and volk are now
fetched by CMake (see above); the actual Vulkan renderer (device/swapchain/
etc.) has not been implemented yet — the current `Renderer` still uses
SDL's own `SDL_Renderer` for the clear/present loop.
