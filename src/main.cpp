// Entry point. Kept intentionally minimal - only knows how to construct and
// run the Application; all SDL details live inside Application/Window/
// Renderer/Game.
//
// Note: this uses a plain int main(argc, argv). SDL3's <SDL3/SDL_main.h>
// convention (which lets SDL provide its own WinMain on Windows, needed for
// some platforms/consoles) is not used yet - it can be added later if/when
// it's actually needed (e.g. to build as a GUI subsystem app with no console
// window).

#include "Application/Application.h"

#include <cstdio>
#include <exception>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    try {
        gte::Application app("Great Tamana Engine", 1280, 720);
        return app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
}
