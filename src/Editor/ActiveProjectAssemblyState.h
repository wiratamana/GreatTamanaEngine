#pragma once

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE3 (PHASE0_MASTER_STRATEGY.md, LDD-CP1). The ONE,
// single, shared concept of "which Project Assembly is currently active" -
// every later campaign in this 5-file series (Open Project, Create Script
// Asset, Compile menu) reads/extends THIS class, never inventing a second,
// competing one. Mirrors ProjectAssemblyHotReloadDebugStatus::Instance()'s
// exact shape (a plain namespace-scope singleton, no dependency injection
// needed, gte_editor-only).
#include <filesystem>
#include <mutex>
#include <string>

namespace gte {

class ProjectAssemblyHost;

// Snapshot returned by GetActive() - a plain value type, safe to read from
// any thread that already holds no other lock (see GetActive()'s own doc
// comment for its own internal locking).
struct ActiveProjectAssemblyInfo {
    bool hasActiveProject = false;
    std::string name;
    std::filesystem::path sourceDirectory; // Projects/<Name>/
    std::filesystem::path assetsDirectory; // Projects/<Name>/Assets/
    bool isCompiled = false; // <Name>_Game.dll exists in the output dir right now.
    bool isLoaded = false;   // its own .dll filename is in ProjectAssemblyHost::GetLoadedAssemblyFileNames().
};

class ActiveProjectAssemblyState {
public:
    static ActiveProjectAssemblyState& Instance();

    // Called by CreateNewProjectAssembly() (this phase) on success, and by
    // a later "Open Project" campaign on a successful open - the ONE place
    // this state is ever set.
    void SetActive(const std::string& name, const std::filesystem::path& sourceDirectory);

    // Re-derives isCompiled/isLoaded FRESH on every single call (a cheap
    // std::filesystem::exists() check plus a linear scan of
    // ProjectAssemblyHost::GetLoadedAssemblyFileNames(), both already
    // cheap, already-existing operations) - never cached, so a Compile
    // that finishes in the background, or an Open that succeeds, is
    // reflected correctly the very next time ANY panel/menu reads this,
    // with no manual "refresh" button. If SetProjectAssemblyHost() has
    // never been called (should only happen in an isolated unit test),
    // isLoaded is always false rather than crashing. LOAD-BEARING: this
    // method locks gte::GetHotReloadEngineStateMutex()
    // (Core/Plugins/HotReloadEngineStateMutex.h) around the
    // GetLoadedAssemblyFileNames() call - mirrors
    // EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()'s own
    // real, current precedent EXACTLY (confirmed by reading that method's
    // real .cpp body) - ProjectAssemblyHost::GetLoadedAssemblyFileNames()
    // itself takes NO internal lock (a plain std::vector), so any caller
    // reading it while a future BIG-STEP 3 hot-reload cycle concurrently
    // mutates m_loadedAssemblies on the main thread would otherwise be a
    // genuine data race - this matters the moment a future "Open Project"
    // campaign exposes this state to a network-thread HTTP route.
    ActiveProjectAssemblyInfo GetActive() const;

    void Clear();

    // Called exactly once, from EditorHost's own constructor body (mirrors
    // EditorHotReloadDebugCapability::SetProjectAssemblyHost()'s own
    // "setter, not a constructor parameter" precedent exactly - this
    // singleton is constructed via ordinary static initialization before
    // any Core/ProjectAssemblyHost object exists).
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
