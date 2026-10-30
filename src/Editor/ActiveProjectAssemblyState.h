#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace gte {

class ProjectAssemblyHost;

// Snapshot returned by GetActive(); a plain value.
struct ActiveProjectAssemblyInfo {
    bool hasActiveProject = false;
    std::string name;
    std::filesystem::path sourceDirectory; // Projects/<Name>/
    std::filesystem::path assetsDirectory; // Projects/<Name>/Assets/
    bool isCompiled = false; // <Name>_Game.dll exists in the output dir right now.
    bool isLoaded = false;   // Its .dll file name is in ProjectAssemblyHost::GetLoadedAssemblyFileNames().
};

// Cheap snapshot returned by GetActiveSource(): name and assets folder only.
struct ActiveProjectSource {
    bool hasActiveProject = false;
    std::string name;
    std::filesystem::path assetsDirectory;
};

// The one shared record of which Project Assembly is active.
class ActiveProjectAssemblyState {
public:
    static ActiveProjectAssemblyState& Instance();

    // Marks a project active. Called on create and on open.
    void SetActive(const std::string& name, const std::filesystem::path& sourceDirectory);

    // Re-derives isCompiled/isLoaded on every call (one stat plus a scan of the loaded list); never cached.
    // Locks the hot-reload engine-state mutex around the loaded-list read.
    ActiveProjectAssemblyInfo GetActive() const;

    // Name and assets folder only. Locks m_mutex; no disk access.
    ActiveProjectSource GetActiveSource() const;

    void Clear();

    // Called once from EditorHost's constructor; a setter because this singleton is built before the host exists.
    void SetProjectAssemblyHost(ProjectAssemblyHost& projectAssemblyHost) noexcept;

private:
    ActiveProjectAssemblyState() = default;

    mutable std::mutex m_mutex;
    bool m_hasActiveProject = false;
    std::string m_name;
    std::filesystem::path m_sourceDirectory;
    ProjectAssemblyHost* m_projectAssemblyHost = nullptr;
};

} // namespace gte
