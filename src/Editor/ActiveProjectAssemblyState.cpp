#include "ActiveProjectAssemblyState.h"

#include "ProjectRootPath.h" // gte::ExecutableDirectory()
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h" // ResolveProjectAssemblyOutputDirectory()
#include "../Core/Plugins/ProjectAssemblyHost.h" // GetLoadedAssemblyFileNames()
// GetHotReloadEngineStateMutex() - REQUIRED around the GetLoadedAssemblyFileNames()
// call inside GetActive() below (see that method's own doc comment in the
// header for the full "why" - ProjectAssemblyHost::GetLoadedAssemblyFileNames()
// itself takes no internal lock).
#include "../Core/Plugins/HotReloadEngineStateMutex.h"

#include <algorithm>

namespace gte {

ActiveProjectAssemblyState& ActiveProjectAssemblyState::Instance()
{
    static ActiveProjectAssemblyState instance;
    return instance;
}

void ActiveProjectAssemblyState::SetActive(const std::string& name, const std::filesystem::path& sourceDirectory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasActiveProject = true;
    m_name = name;
    m_sourceDirectory = sourceDirectory;
}

void ActiveProjectAssemblyState::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasActiveProject = false;
    m_name.clear();
    m_sourceDirectory.clear();
}

void ActiveProjectAssemblyState::SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept
{
    m_projectAssemblyHost = &projectAssemblyHost;
}

ActiveProjectAssemblyInfo ActiveProjectAssemblyState::GetActive() const
{
    ActiveProjectAssemblyInfo info;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasActiveProject) {
            return info; // hasActiveProject stays false; everything else default.
        }
        info.hasActiveProject = true;
        info.name = m_name;
        info.sourceDirectory = m_sourceDirectory;
    }
    info.assetsDirectory = info.sourceDirectory / "Assets";

    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    info.isCompiled = std::filesystem::exists(outputDirectory / (info.name + "_Game.dll"));

    if (m_projectAssemblyHost != nullptr) {
        // Mirrors EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()'s
        // own real, current precedent EXACTLY - ProjectAssemblyHost::
        // GetLoadedAssemblyFileNames() itself takes no internal lock, so this
        // read must be guarded by the SAME shared mutex a future hot-reload
        // cycle's own mutation of m_loadedAssemblies is required to hold too
        // (HotReloadEngineStateMutex.h's own header comment) - without this,
        // a future "Open Project"/HTTP-exposed caller of GetActive() from a
        // network thread would race a main-thread hot-reload cycle.
        std::lock_guard<std::mutex> loadedAssembliesLock(GetHotReloadEngineStateMutex());
        const std::vector<std::string> loaded = m_projectAssemblyHost->GetLoadedAssemblyFileNames();
        info.isLoaded = std::find(loaded.begin(), loaded.end(), info.name + "_Game.dll") != loaded.end();
    }
    return info;
}

} // namespace gte
