#include "ProjectRootPath.h"

#include <SDL3/SDL.h>

namespace gte {

std::filesystem::path ResolveProjectRootDirectory()
{
    // SDL_GetBasePath() returns the directory containing the running
    // executable (with a trailing separator), UTF-8 encoded, owned by SDL
    // (never freed by us). Constructed via std::u8string (path's own
    // C++20 UTF-8-aware constructor) rather than a bare
    // std::filesystem::path(std::string) construction, which would
    // otherwise go through the OS's native narrow encoding instead of
    // UTF-8 on Windows - the same reasoning ProjectPanelData.h's own
    // Utf8ToPath() documents, duplicated here (as a small, deliberate,
    // documented exception) rather than reused, since that header is only
    // compiled when GTE_ENABLE_PROJECT_PANEL is ON and this helper must
    // work regardless of that switch.
    const char* basePath = SDL_GetBasePath();
    const std::string basePathUtf8 = (basePath != nullptr) ? basePath : "./";
    return std::filesystem::path(std::u8string(basePathUtf8.begin(), basePathUtf8.end())) / "Project";
}

} // namespace gte
