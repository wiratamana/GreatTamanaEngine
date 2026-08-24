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

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

namespace {

// A tiny, headless "--reimport <source-file> <destination-.gta-path>" CLI
// mode - runs AssetImporter.h's ImportAssetFile() (the exact same PMX/VMD/
// image -> *.gta pipeline the Editor's "Project" panel drag-and-drop uses)
// with NO Window/Renderer/GPU device created at all, and prints its
// message. Exists purely as an ops/automation escape hatch for re-running
// an import from a script/CI/AI-agent context where driving the real ImGui
// Editor UI isn't practical - e.g. regenerating an existing *.gta after a
// PmxLoader.cpp change (like adding material/texture import support) picks
// up data an earlier import predates. Returns the process exit code (0 on
// success, 1 on failure) - main() below returns this directly instead of
// falling through to the normal Application/Window/Renderer startup path.
int RunReimportCli(const std::string& sourcePath, const std::string& destinationGtaPath)
{
    gte::AssetDatabase database; // Only used for its ImportAsset()/Guid bookkeeping - no directory scan needed here.
    const gte::AssetImportResult result =
        gte::ImportAssetFile(database, std::filesystem::path(sourcePath), std::filesystem::path(destinationGtaPath));
    std::fprintf(stdout, "%s\n", result.message.c_str());
    return result.success ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc >= 4 && std::string(argv[1]) == "--reimport") {
        return RunReimportCli(argv[2], argv[3]);
    }

    try {
        gte::Application app("Great Tamana Engine", 1280, 720);
        return app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
}
