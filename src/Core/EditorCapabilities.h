#pragma once

#include "GraphicsHandles.h"

// Small single-purpose interfaces for editor capabilities. Core code holds a nullable pointer to each;
// a standalone host that registers none gets a safe "not available" answer.

#include "Logging.h" // LogEntry/LogQueryFilter for ILogQueryCapability.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gte {

class Game;
class Renderer;

// Saves and loads scenes through the editor. `outErrorMessage` is set only when a call returns false.
class ISceneIOCapability {
public:
    virtual ~ISceneIOCapability() = default;

    // True on success. Creates the parent directory first; returns false and sets `outErrorMessage` on I/O failure.
    virtual bool SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage) = 0;

    // True when the scene was fully replaced. On failure (missing file, parse error) the current scene is left
    // untouched and `outErrorMessage` is set. `sceneServicesSetLayout` is Core's scene services layout.
    virtual bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath,
        DescriptorSetLayoutHandle sceneServicesSetLayout, std::string& outErrorMessage) = 0;

    // The default scene path used when a caller supplies an empty one.
    virtual std::filesystem::path DefaultScenePath() const = 0;

    // Result of OpenSponzaScene(); `message` is a status line on success, the error text on failure.
    struct OpenSceneOutcome {
        bool success = false;
        bool importedThisCall = false;
        std::string message;
        std::string gtaPath;
    };

    // Replaces the scene with the Sponza glTF sample, importing it first if needed. Main thread only.
    virtual OpenSceneOutcome OpenSponzaScene(
        Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout) = 0;
};

// Log query capability. Core code holds a nullable pointer: nullptr means the build has no logger,
// which is distinct from IsEnabled() returning false.
class ILogQueryCapability {
public:
    virtual ~ILogQueryCapability() = default;

    virtual std::vector<LogEntry> Query(const LogQueryFilter& filter) = 0;
    virtual void Clear() = 0;
    virtual std::size_t EntryCount() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual std::uint64_t LatestEntryId() const = 0;
};

// Live state of project assembly hot reload, plus triggers for a compile or a reload. A nullable pointer:
// nullptr means every backed route answers 503. Methods that read the ECS registry run on the main thread only.
class IHotReloadDebugCapability {
public:
    virtual ~IHotReloadDebugCapability() = default;

    // "Idle" when no cycle is running. lastOutcome/lastErrorMessage describe the most recent finished cycle
    // and stay set after returning to Idle ("" before the first one).
    struct Status {
        std::string phase = "Idle";
        std::string projectName;
        std::uint64_t cycleId = 0;
        std::uint64_t phaseElapsedMilliseconds = 0;
        std::string lastOutcome;       // "" | "Success" | "RolledBack" | "CriticalFailure"
        std::string lastErrorMessage;  // "" unless lastOutcome needs explaining
    };
    virtual Status GetHotReloadStatus() const = 0;

    // What one project assembly registered; currently every list is empty.
    struct LedgerEntry {
        std::vector<std::string> renderPassNames;
        std::vector<std::string> renderFeatureNames;
        std::vector<std::string> panelNames;
        std::vector<std::string> componentTypeNames;
    };
    virtual LedgerEntry GetLedgerEntry(const std::string& projectName) const = 0;

    // File names of the loaded project assemblies.
    virtual std::vector<std::string> GetLoadedAssemblyFileNames() const = 0;

    // Registered component type names, sorted.
    virtual std::vector<std::string> GetRegisteredComponentTypeNames() const = 0;

    // The same scene JSON as Save Scene, built in memory. Main thread only, with a live Game&.
    virtual std::string BuildSceneSnapshotJson(Game& game) = 0;

    // Starts a background compile. Returns false only when a build for this project is already running.
    virtual bool TriggerCompileOnly(const std::string& projectName) = 0;

    // True while a build for this project is running.
    virtual bool IsCompileInFlight(const std::string& projectName) const = 0;

    // Compile plus reload. Currently always returns false and does nothing.
    virtual bool TriggerHotReload(const std::string& projectName) = 0;

    // Sets the "value" field of the "ProbeHotReloadMarker" component on the entity that carries it. False when no
    // such entity exists or the request cannot be served. Test-only; never widen it into a general field setter.
    virtual bool SetProbeHotReloadMarkerValueForTesting(int value) = 0;
};

// Creates a project's on-disk source folder. A nullable pointer: nullptr means every backed route answers 503.
class IProjectLifecycleCapability {
public:
    virtual ~IProjectLifecycleCapability() = default;

    struct CreateProjectOutcome {
        bool success = false;
        std::string errorMessage; // only meaningful when success == false
        std::string createdSourceDirectory; // absolute path, only meaningful when success == true
    };

    // Validates `name`, rejects an existing folder or file, writes the 3-file scaffold and makes it the active
    // project. Filesystem plus a synchronous cmake reconfigure; safe from any thread.
    virtual CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) = 0;

    // Outcome of one "Open Project" attempt.
    struct OpenProjectOutcome {
        bool success = false;         // true even for NotBuildable/NotCompiled tiers; false only for NotAProject or an internal error
        std::string errorMessage;     // only meaningful when success == false
        std::string statusMessage;    // human-readable outcome, ALWAYS populated when success == true
        bool loadAttempted = false;
        bool loadSucceeded = false;
    };

    // Plain value type for ListProjectAssemblies().
    struct ProjectListEntry {
        std::string name;
        std::string tierName; // "NotAProject" / "NotBuildable" / "NotCompiled" / "Compiled" / "AlreadyLoaded"
    };

    // Any thread except the main thread: a compiled project blocks until the main thread performs the load,
    // so calling this from the main thread deadlocks (use OpenProjectAssemblyOnMainThread()).
    virtual OpenProjectOutcome OpenProjectAssembly(const std::string& name) = 0;

    // Main thread only. Same as OpenProjectAssembly() but loads inline, with no cross-thread bridge.
    virtual OpenProjectOutcome OpenProjectAssemblyOnMainThread(const std::string& name) = 0;

    // Any thread. Read-only enumeration of the folders under the project source root.
    virtual std::vector<ProjectListEntry> ListProjectAssemblies() = 0;
};

// Scaffolds a render pass or shader source file into the active project's Assets/ folder.
// A nullable pointer: nullptr means every backed route answers 503.
enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair, ScreenPostProcessPass };

class IAssetScaffoldingCapability {
public:
    virtual ~IAssetScaffoldingCapability() = default;

    struct ScaffoldOutcome {
        bool success = false;
        std::string errorMessage;              // meaningful only when success == false
        std::vector<std::string> createdFiles; // relative to Assets/, meaningful only when success == true
        std::string reminderMessage;           // "" for ShaderPair (no companion .cpp is generated for that kind)
    };

    // Any thread. Pure filesystem I/O against the active project; no CMake reconfigure (the next build's glob picks the files up).
    virtual ScaffoldOutcome CreateAssetScaffold(AssetScaffoldKind kind, const std::string& name) = 0;
};

} // namespace gte
