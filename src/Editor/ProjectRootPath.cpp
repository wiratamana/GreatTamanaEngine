#include "ProjectRootPath.h"

#include "../Encoding/Utf8Path.h"
#include <SDL3/SDL.h>

namespace gte {

namespace {

// Directory of the running executable (SDL returns UTF-8 with a trailing separator).
std::filesystem::path ResolveExecutableDirectoryBasePath()
{
    const char* basePath = SDL_GetBasePath();
    return PathFromUtf8((basePath != nullptr) ? std::string_view(basePath) : std::string_view("./"));
}

} // namespace

std::filesystem::path ResolveProjectRootDirectory()
{
    return ResolveExecutableDirectoryBasePath() / "Project";
}

std::filesystem::path ExecutableDirectory()
{
    return ResolveExecutableDirectoryBasePath();
}

} // namespace gte
