cmake_minimum_required(VERSION 3.19)

# --- Platform gate -----------------------------------------------------
# This build currently only supports Windows. Everything else is a quick,
# explicit failure rather than attempting (and likely breaking) a build.
if(NOT WIN32)
    message(FATAL_ERROR
        "GreatTamanaEngine's CMake build only supports Windows right now. "
        "Building on this platform is not supported.")
endif()

project(GreatTamanaEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(FetchSDL3)

# Downloads (if not already present) and stages SDL3 for Windows:
#   include/SDL3/*.h  - headers
#   SDL3.dll          - runtime binary
#   lib/SDL3.lib      - import library, for linking
# Also defines the SDL3::SDL3 imported target used below.
fetch_sdl3()

add_executable(GreatTamanaEngine
    src/main.cpp
    src/Application/Application.cpp
    src/Application/Application.h
    src/Application/EventTranslator.cpp
    src/Application/EventTranslator.h
    src/Event/Event.h
    src/Input/InputState.cpp
    src/Input/InputState.h
    src/Window/Window.cpp
    src/Window/Window.h
    src/Renderer/Renderer.cpp
    src/Renderer/Renderer.h
    src/Game/Game.cpp
    src/Game/Game.h
)

target_include_directories(GreatTamanaEngine PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(GreatTamanaEngine PRIVATE SDL3::SDL3)

# Ensures SDL3.dll is copied next to the built .exe after every build.
sdl3_copy_runtime_dll(GreatTamanaEngine)
