// Entry point: constructs and runs EditorHost. All SDL, window, renderer and
// editor details live behind it. Plain int main (no SDL_main).

#include "Editor/EditorHost.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

namespace {

// Headless "--reimport <source-file> <destination-.gta-path>" mode: runs ImportAssetFile()
// with no window or GPU device and prints its messages. Returns the exit code (0 = success).
int RunReimportCli(const std::string& sourcePath, const std::string& destinationGtaPath)
{
    gte::AssetDatabase database; // Only used for ImportAsset() bookkeeping; no directory scan needed.
    const gte::AssetImportResult result = gte::ImportAssetFile(
        database, std::filesystem::path(sourcePath), std::filesystem::path(destinationGtaPath), gte::MeshImportOptions{});
    std::fprintf(stdout, "%s\n", result.message.c_str());
    if (!result.clusterMessage.empty()) {
        std::fprintf(stdout, "%s\n", result.clusterMessage.c_str());
    }
    return result.success ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc >= 4 && std::string(argv[1]) == "--reimport") {
        return RunReimportCli(argv[2], argv[3]);
    }

    try {
        gte::EditorHost host("Great Tamana Engine", 1280, 720);
        return host.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
}
