#include "ActiveProjectAssemblyState.h"

#include "ProjectRootPath.h" // gte::ExecutableDirectory()
#include "../Core/Plugins/ProjectAssemblyBuildRunner.h" // ResolveProjectAssemblyOutputDirectory()
#include "../Core/Plugins/ProjectAssemblyHost.h" // GetLoadedAssemblyFileNames()
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

ActiveProjectSource ActiveProjectAssemblyState::GetActiveSource() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ActiveProjectSource source;
    if (m_hasActiveProject) {
        source.hasActiveProject = true;
        source.name = m_name;
        source.assetsDirectory = m_sourceDirectory / "Assets";
    }
    return source;
}

ActiveProjectAssemblyInfo ActiveProjectAssemblyState::GetActive() const
{
    ActiveProjectAssemblyInfo info;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasActiveProject) {
            return info;
        }
        info.hasActiveProject = true;
        info.name = m_name;
        info.sourceDirectory = m_sourceDirectory;
    }
    info.assetsDirectory = info.sourceDirectory / "Assets";

    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    info.isCompiled = std::filesystem::exists(outputDirectory / (info.name + "_Game.dll"));

    if (m_projectAssemblyHost != nullptr) {
        // The host's loaded list has no lock of its own; hot reload mutates it under this mutex.
        std::lock_guard<std::mutex> loadedAssembliesLock(GetHotReloadEngineStateMutex());
        const std::vector<std::string> loaded = m_projectAssemblyHost->GetLoadedAssemblyFileNames();
        info.isLoaded = std::find(loaded.begin(), loaded.end(), info.name + "_Game.dll") != loaded.end();
    }
    return info;
}

} // namespace gte
