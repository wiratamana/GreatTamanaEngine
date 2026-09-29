// src/Core/Plugins/ProjectAssemblyBuildRunner.cpp
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE6.
// See ProjectAssemblyBuildRunner.h's own header comment for the full design
// rationale (background-thread choice, shutdown-behavior decision).
#include "ProjectAssemblyBuildRunner.h"

#include "../Logging.h"
#include "../../Jobs/JobSystem.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gte {

namespace {

// Guards against two overlapping `cmake --build` child processes racing
// each other's output for the SAME project - see this file's own header
// comment. Calling TriggerProjectAssemblyCompile() again for a DIFFERENT
// project while one is already in flight is fine (a separate entry in this
// set), matching this file's own documented contract.
std::mutex g_inFlightMutex;
std::unordered_set<std::string> g_inFlightProjects;

bool TryMarkInFlight(const std::string& projectName)
{
    std::lock_guard<std::mutex> lock(g_inFlightMutex);
    if (g_inFlightProjects.count(projectName) != 0) {
        return false;
    }
    g_inFlightProjects.insert(projectName);
    return true;
}

void ClearInFlight(const std::string& projectName)
{
    std::lock_guard<std::mutex> lock(g_inFlightMutex);
    g_inFlightProjects.erase(projectName);
}

// This repo's own request/response strings are assumed ASCII/UTF-8
// throughout (see e.g. Network/NetworkServer.cpp's own JSON bodies) - a
// plain MultiByteToWideChar(CP_UTF8, ...) round-trip is enough; no locale
// dependency.
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }
    const int requiredChars =
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (requiredChars <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(requiredChars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), requiredChars);
    return wide;
}

std::string ToLowerAscii(const std::string& text)
{
    std::string lower = text;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

// Quotes a single argument for a CreateProcessW() lpCommandLine string,
// following the standard Windows/MSVCRT argv-splitting convention (the same
// one CommandLineToArgvW() parses). Needed because `buildDirectory` is a
// directory path that, per SDL_GetBasePath()'s own documented convention
// (gte::ExecutableDirectory()'s underlying implementation), ALWAYS ends
// with a trailing path separator - naively wrapping a value with a trailing
// backslash in a plain "\"...\"" pair is a real, confirmed bug: a backslash
// immediately before the closing quote escapes that quote instead of
// terminating the argument, silently merging the REST of the command line
// into the same argument (reproduced live: "cmake --build \"...\\build\\\"
// --target ProjectAssemblyProbe_Game" was parsed by cmake as ONE single
// path argument, "...\\build\" --target ProjectAssemblyProbe_Game",
// which cmake correctly rejected as "is not a directory"). This function
// doubles any run of backslashes that is immediately followed by a quote
// (embedded or final), so the resulting argument round-trips exactly.
std::wstring QuoteWindowsArgument(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument; // No special characters - quoting is unnecessary.
    }

    std::wstring quoted = L"\"";
    std::size_t backslashRun = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashRun;
            continue;
        }
        if (c == L'"') {
            quoted.append(backslashRun * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashRun = 0;
            continue;
        }
        quoted.append(backslashRun, L'\\');
        backslashRun = 0;
        quoted.push_back(c);
    }
    // Trailing backslashes right before the closing quote must be doubled -
    // this is the exact case that caused the bug this function fixes.
    quoted.append(backslashRun * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE1. Generic "spawn this exact command line, stream its
// combined stdout/stderr into GTE_LOG_INFO/WARNING/ERROR (logCategory)
// line-by-line, block until it exits" - the ONE real child-process
// mechanism this whole file uses, shared by RunOneBuildTarget() (existing,
// refactored by this phase to call this) and RunPlainCMakeReconfigureAndWait()
// (new, this phase). Returns the child's real exit code, or a negative
// sentinel if the process could not even be created/piped (mirrors
// RunOneBuildTarget()'s own former exact contract). `onLine`, if provided
// (RunOneBuildTarget() provides one; RunPlainCMakeReconfigureAndWait()
// passes the default, empty one - it needs no per-line inspection, only
// the final exit code), is invoked once per completed output line,
// ADDITIONALLY to (never instead of) this function's own internal
// keyword-based error/warning/info logging below - callers must not
// assume their own onLine call means the line was not already logged.
int RunChildProcessAndWait(const std::wstring& commandLine, const std::filesystem::path& workingDirectory,
    const char* logCategory, const std::function<void()>& onIdleTick,
    const std::function<void(const std::string&)>& onLine = {})
{
    SECURITY_ATTRIBUTES pipeSecurityAttributes{};
    pipeSecurityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    pipeSecurityAttributes.bInheritHandle = TRUE;
    pipeSecurityAttributes.lpSecurityDescriptor = nullptr;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &pipeSecurityAttributes, 0)) {
        GTE_LOG_ERROR(logCategory, std::string("CreatePipe() failed - cannot capture child process output."));
        return -1;
    }
    // The parent's own read end must never be inherited by the child - only
    // the write end (handed to the child as its stdout/stderr) is meant to
    // be inherited.
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    const std::wstring workingDirectoryWide = workingDirectory.wstring();

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = nullptr;

    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        nullptr, mutableCommandLine.data(), nullptr, nullptr, /*bInheritHandles=*/TRUE,
        CREATE_NO_WINDOW, nullptr, workingDirectoryWide.c_str(), &startupInfo, &processInfo);

    // The parent's own copy of the write end must be closed right after
    // CreateProcessW(), regardless of success/failure - otherwise ReadFile()
    // below would never see end-of-pipe (it would think the write end is
    // still open, via this leftover parent handle, even after the child
    // itself exits).
    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        GTE_LOG_ERROR(logCategory,
            "CreateProcessW() failed (GetLastError=" + std::to_string(GetLastError()) + ").");
        return -1;
    }

    std::string lineBuffer;
    char readBuffer[4096];
    for (;;) {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
            break; // Pipe closed (child exited) or a genuine error - both end this loop, matching ReadFile()'s own former "false -> stop" contract.
        }
        if (bytesAvailable == 0) {
            if (onIdleTick) {
                onIdleTick();
            }
            Sleep(50); // Fixed poll cadence - see RunOneBuildTarget()'s own former doc comment (PHASE2).
            continue;
        }
        DWORD bytesRead = 0;
        if (!ReadFile(readPipe, readBuffer, sizeof(readBuffer), &bytesRead, nullptr) || bytesRead == 0) {
            break;
        }
        lineBuffer.append(readBuffer, bytesRead);
        std::size_t newlinePos = 0;
        while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {
            std::string line = lineBuffer.substr(0, newlinePos);
            lineBuffer.erase(0, newlinePos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            const std::string lower = ToLowerAscii(line);
            if (lower.find("error") != std::string::npos) {
                GTE_LOG_ERROR(logCategory, line);
            } else if (lower.find("warning") != std::string::npos) {
                GTE_LOG_WARNING(logCategory, line);
            } else {
                GTE_LOG_INFO(logCategory, line);
            }
            if (onLine) {
                onLine(line);
            }
        }
    }
    if (!lineBuffer.empty()) {
        GTE_LOG_INFO(logCategory, lineBuffer);
        if (onLine) {
            onLine(lineBuffer);
        }
    }
    CloseHandle(readPipe);

    // Blocks (this IS this thread's whole job) until the child fully exits
    // - by this point its own stdout/stderr pipe has already closed
    // (ReadFile() returned false above), so this should return immediately
    // in practice, but WaitForSingleObject() is still the correct,
    // unambiguous way to retrieve the real exit code.
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    return static_cast<int>(exitCode);
}

// Runs ONE `cmake --build <buildDirectory> --target <targetName>`
// invocation via the shared RunChildProcessAndWait() helper above (PHASE1,
// editor-core-separation-16 campaign - this function's own former inline
// CreateProcessW()/pipe-drain body was extracted into that generic helper;
// this function's behavior is otherwise unchanged). Returns the child's
// real exit code, or a negative sentinel if the process itself could not
// even be created/piped. `targetMissingHeuristicHit` is set true if the
// output looks like Ninja/CMake reporting that `targetName` simply does
// not exist - the caller uses this to distinguish "no Editor/ sources,
// perfectly normal" from a genuine build failure (this phase's own
// "STEP 2" doc-file reasoning) - detected here via a dedicated `onLine`
// callback that does NOT re-log the line itself (RunChildProcessAndWait()'s
// own internal keyword-based classification already logged it once).
// `onIdleTick`, if non-empty, is invoked once per poll iteration whenever
// no build output is currently available - pass an empty std::function
// (the default) from any call site that is NOT running on the
// main/window-owning thread.
int RunOneBuildTarget(const std::string& buildDirectory, const std::string& targetName, bool& targetMissingHeuristicHit,
    const std::function<void()>& onIdleTick = {})
{
    targetMissingHeuristicHit = false;

    const std::wstring buildDirectoryWide = Utf8ToWide(buildDirectory);
    // QuoteWindowsArgument() (above) - NOT a naive "\"...\"" wrap - see that
    // function's own doc comment for the exact bug this avoids (a trailing
    // backslash, always present on buildDirectory per SDL_GetBasePath()'s
    // convention, would otherwise escape the closing quote).
    const std::wstring commandLine = L"cmake --build " + QuoteWindowsArgument(buildDirectoryWide)
        + L" --target " + QuoteWindowsArgument(Utf8ToWide(targetName));

    bool* const targetMissingHeuristicHitPtr = &targetMissingHeuristicHit;
    const std::function<void(const std::string&)> onLine = [targetMissingHeuristicHitPtr](const std::string& line) {
        const std::string lower = ToLowerAscii(line);
        if (lower.find("unknown target") != std::string::npos
            || lower.find("no rule to make target") != std::string::npos
            || lower.find("targets not built") != std::string::npos) {
            *targetMissingHeuristicHitPtr = true;
        }
    };

    // buildDirectoryWide is constructed directly from the SAME
    // std::filesystem::path type RunChildProcessAndWait() expects, avoiding
    // a second, potentially divergent re-encoding of buildDirectory.
    return RunChildProcessAndWait(commandLine, std::filesystem::path(buildDirectoryWide), "ProjectAssemblyBuild", onIdleTick, onLine);
}

// The actual background-thread body, registered via
// JobSystem::RegisterBackgroundThread() (never Schedule() - see this file's
// own header comment). PHASE2 (editor-core-separation-14 campaign)
// extracted the actual Game-then-Editor build sequencing into the new,
// shared, non-static gte::RunProjectAssemblyBuildAndWait() below - this
// function is now a thin wrapper around it, preserving its EXACT existing
// final log line text/exit-code semantics (a hot-reload caller must never
// see this "relaunch GreatTamanaEditor.exe..." wording - see the NEW
// synchronous path's own, different final line instead).
void RunBuildThreadBody(std::string projectName, std::string buildDirectory, std::shared_ptr<std::atomic<bool>> completionFlag)
{
    // No onIdleTick - a background thread owns no window to pump messages
    // for; doing so would be a pointless no-op.
    const BuildOutcome outcome = RunProjectAssemblyBuildAndWait(projectName, buildDirectory);
    // This exact reminder MUST appear in the final log line, every time
    // (success or failure) - see this phase's own doc file, and LDD4
    // (Project Assemblies are never hot-reloaded).
    GTE_LOG_INFO("ProjectAssemblyBuild",
        "Build finished with exit code " + std::to_string(outcome.exitCode) +
        " - relaunch GreatTamanaEditor.exe to use the result (Project Assemblies are not hot-reloaded, see PHASE0_MASTER_STRATEGY.md, LDD4).");

    ClearInFlight(projectName);
    // Set right before returning, per JobSystem::RegisterBackgroundThread()'s
    // own documented contract (mirrors JobContinuation.cpp's identical
    // precedent).
    completionFlag->store(true, std::memory_order_release);
}

} // namespace

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE2. See this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full contract. Extracted from
// RunBuildThreadBody's own former inline body (this phase) - identical
// Game-then-Editor sequencing/target-missing heuristic, byte-for-byte.
BuildOutcome RunProjectAssemblyBuildAndWait(const std::string& projectName, const std::string& buildDirectory,
    const std::function<void()>& onIdleTick)
{
    GTE_LOG_INFO("ProjectAssemblyBuild",
        "Starting build for Project Assembly '" + projectName + "' (build directory: " + buildDirectory + ")...");

    bool gameTargetMissing = false;
    const int gameExitCode = RunOneBuildTarget(buildDirectory, projectName + "_Game", gameTargetMissing, onIdleTick);

    int editorExitCode = 0;
    if (gameExitCode == 0) {
        bool editorTargetMissing = false;
        editorExitCode = RunOneBuildTarget(buildDirectory, projectName + "_Editor", editorTargetMissing, onIdleTick);
        if (editorExitCode != 0 && editorTargetMissing) {
            GTE_LOG_INFO("ProjectAssemblyBuild",
                "Project '" + projectName + "' has no '_Editor' target (no Editor/ sources) - this is normal, not a build failure.");
            editorExitCode = 0;
        }
    } else {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "'" + projectName + "_Game' target build failed (exit code " + std::to_string(gameExitCode) + ") - skipping the '_Editor' target attempt.");
    }

    BuildOutcome outcome;
    outcome.exitCode = (gameExitCode != 0) ? gameExitCode : editorExitCode;
    outcome.success = (outcome.exitCode == 0);
    return outcome;
}

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE1. See this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full contract. Reuses the SAME
// shared RunChildProcessAndWait() helper RunOneBuildTarget() uses
// internally (defined above, in this file's own anonymous namespace) -
// never a second, independently-written child-process mechanism. Neither
// onIdleTick nor onLine are needed here (this call site has no message
// pump to service, and no per-line inspection of its own - only the
// final exit code matters).
bool RunPlainCMakeReconfigureAndWait(const std::filesystem::path& sourceDirectory, const std::filesystem::path& buildDirectory)
{
    const std::wstring commandLine = L"cmake -S " + QuoteWindowsArgument(sourceDirectory.wstring())
        + L" -B " + QuoteWindowsArgument(buildDirectory.wstring());

    // buildDirectory is used as the child's working directory - it is
    // guaranteed to already exist (ResolveCMakeBuildDirectory() only ever
    // returns a directory that already contains a real CMakeCache.txt),
    // unlike sourceDirectory, which this function must tolerate being
    // bogus/non-existent (see this phase's own
    // PlainReconfigureFailsCleanlyAgainstANonExistentSourceDirectory test).
    const int exitCode = RunChildProcessAndWait(commandLine, buildDirectory, "ProjectAssemblyBuild", {}, {});
    if (exitCode != 0) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "RunPlainCMakeReconfigureAndWait() - 'cmake -S " + sourceDirectory.string() + " -B " +
            buildDirectory.string() + "' failed (exit code " + std::to_string(exitCode) + ").");
        return false;
    }
    return true;
}

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE2. See this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full contract - shares the EXACT
// SAME per-project in-flight guard (g_inFlightProjects, anonymous namespace
// above) as the existing async path (TriggerProjectAssemblyCompile).
bool TryRunProjectAssemblyBuildSynchronously(const std::string& projectName, const std::string& buildDirectory,
    BuildOutcome& outOutcome, const std::function<void()>& onIdleTick)
{
    if (!TryMarkInFlight(projectName)) {
        GTE_LOG_WARNING("ProjectAssemblyBuild",
            "Synchronous build for Project Assembly '" + projectName + "' rejected - a build for this project is already in progress.");
        return false;
    }
    outOutcome = RunProjectAssemblyBuildAndWait(projectName, buildDirectory, onIdleTick);
    ClearInFlight(projectName);
    return true;
}

bool TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory)
{
    if (!TryMarkInFlight(projectName)) {
        GTE_LOG_WARNING("ProjectAssemblyBuild",
            "A build for Project Assembly '" + projectName + "' is already in progress - ignoring this new request.");
        return false;
    }

    auto completionFlag = std::make_shared<std::atomic<bool>>(false);
    std::thread buildThread(&RunBuildThreadBody, projectName, buildDirectory, completionFlag);
    // JobSystem::RegisterBackgroundThread() - NEVER JobSystem::Schedule(),
    // which runs on a FIXED-size worker pool shared by every other engine
    // subsystem - see this file's own header comment for the full
    // reasoning, and JobContinuation.cpp's WatchDependencyWithFallback()
    // for the real, working precedent this mirrors.
    gte::Jobs::JobSystem::Instance().RegisterBackgroundThread(std::move(buildThread), completionFlag);
    return true;
}

// editor-core-separation-19 campaign (On-Engine Project Workflow plan,
// BIG-STEP 5), PHASE1. See this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full contract.
bool IsProjectAssemblyBuildInFlight(const std::string& projectName)
{
    std::lock_guard<std::mutex> lock(g_inFlightMutex);
    return g_inFlightProjects.count(projectName) != 0;
}

std::filesystem::path ResolveCMakeBuildDirectory(const std::filesystem::path& startDirectory, int maxParentLevels)
{
    std::filesystem::path current = startDirectory;
    for (int level = 0; level <= maxParentLevels; ++level) {
        std::error_code existsError;
        if (std::filesystem::exists(current / "CMakeCache.txt", existsError)) {
            return current;
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }
    GTE_LOG_ERROR("ProjectAssemblyBuild",
        "Could not find a CMakeCache.txt within " + std::to_string(maxParentLevels) +
        " parent director" + std::string(maxParentLevels == 1 ? "y" : "ies") +
        " starting from " + startDirectory.string() + " - cannot resolve the CMake build directory.");
    return std::filesystem::path();
}

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE1. See this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full contract.
std::filesystem::path ResolveProjectAssemblySourceRootDirectory(const std::filesystem::path& buildDirectory)
{
    const std::filesystem::path cachePath = buildDirectory / "CMakeCache.txt";
    std::ifstream file(cachePath);
    if (!file.is_open()) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "ResolveProjectAssemblySourceRootDirectory: could not open " + cachePath.string());
        return {};
    }
    constexpr const char* kPrefix = "CMAKE_HOME_DIRECTORY:INTERNAL=";
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind(kPrefix, 0) == 0) {
            std::filesystem::path sourceRoot(line.substr(std::string(kPrefix).length()));
            return sourceRoot / "Projects";
        }
    }
    GTE_LOG_ERROR("ProjectAssemblyBuild",
        "ResolveProjectAssemblySourceRootDirectory: no CMAKE_HOME_DIRECTORY line found in " + cachePath.string());
    return {};
}

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4 - see this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full layering-constraint reasoning
// (this function NEVER calls gte::ExecutableDirectory() itself).
std::filesystem::path ResolveProjectAssemblyOutputDirectory(const std::filesystem::path& executableDirectory)
{
    return executableDirectory / "project_assemblies";
}

// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE1. See this function's own doc comment in
// ProjectAssemblyBuildRunner.h for the full contract. Mirrors
// gte_add_project()'s own real, current body (cmake/GteProject.cmake) -
// re-confirmed by directly reading it before writing this: ONE recursive
// glob (file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${ASSETS}/*.cpp"))
// over Assets/, bucketed into GAME_SOURCES/EDITOR_SOURCES purely by
// whether each file's own absolute path contains the literal substring
// "/Editor/" anywhere at any depth (if(SRC MATCHES "/Editor/")) - a .cpp
// file nested several folders deep under Assets/ is still real, buildable
// Game source as far as CMake is concerned, so this uses
// recursive_directory_iterator (never a plain, one-level
// directory_iterator), and checks generic_string() (always forward
// slashes, regardless of platform) so this substring check behaves
// identically to CMake's own match on an absolute path.
ProjectValidityTier ClassifyProjectAssemblyFolder(
    const std::filesystem::path& candidateFolder,
    const std::filesystem::path& outputDirectory,
    const std::vector<std::string>& loadedDllFileNames)
{
    std::error_code isDirectoryError;
    if (!std::filesystem::is_directory(candidateFolder, isDirectoryError)) {
        return ProjectValidityTier::NotAProject;
    }
    std::error_code cmakeListsExistsError;
    if (!std::filesystem::exists(candidateFolder / "Libraries" / "CMakeLists.txt", cmakeListsExistsError)) {
        return ProjectValidityTier::NotAProject;
    }

    // RECURSIVE, mirroring gte_add_project()'s own GLOB_RECURSE - a .cpp
    // file nested several folders deep under Assets/ is still real,
    // buildable Game source as far as CMake is concerned, so a plain
    // one-level directory_iterator here would silently disagree with the
    // build system and misclassify a valid project as NotBuildable.
    bool hasRealGameSource = false;
    std::error_code iterationError;
    const std::filesystem::path assetsDirectory = candidateFolder / "Assets";
    if (std::filesystem::is_directory(assetsDirectory, iterationError)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDirectory, iterationError)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
                continue;
            }
            // generic_string() always uses forward slashes regardless of
            // platform, so this substring check behaves identically to
            // CMake's own `if(SRC MATCHES "/Editor/")` on this same
            // absolute path - never re-derive this from a relative path,
            // which could disagree with CMake's own ABSOLUTE-path match.
            if (entry.path().generic_string().find("/Editor/") != std::string::npos) {
                continue; // Editor-only source - never counts as Game source.
            }
            hasRealGameSource = true;
            break;
        }
    }
    if (!hasRealGameSource) {
        return ProjectValidityTier::NotBuildable;
    }

    const std::string name = candidateFolder.filename().string();
    const std::string dllFileName = name + "_Game.dll";
    std::error_code dllExistsError;
    if (!std::filesystem::exists(outputDirectory / dllFileName, dllExistsError)) {
        return ProjectValidityTier::NotCompiled;
    }

    const bool alreadyLoaded = std::find(loadedDllFileNames.begin(), loadedDllFileNames.end(), dllFileName)
        != loadedDllFileNames.end();
    return alreadyLoaded ? ProjectValidityTier::AlreadyLoaded : ProjectValidityTier::Compiled;
}

namespace {

// editor-core-separation-13 campaign, PHASE4 - the shared backup-slot
// naming convention both BackupProjectAssemblyBinaries() and
// RestoreProjectAssemblyBinariesFromBackup() use, kept in exactly one
// place so the two halves can never independently drift apart.
std::filesystem::path BackupDirectoryFor(const std::filesystem::path& outputDirectory)
{
    return outputDirectory / ".hotreload_backup";
}

std::filesystem::path GameDllPath(const std::filesystem::path& outputDirectory, const std::string& projectName)
{
    return outputDirectory / (projectName + "_Game.dll");
}

std::filesystem::path EditorDllPath(const std::filesystem::path& outputDirectory, const std::string& projectName)
{
    return outputDirectory / (projectName + "_Editor.dll");
}

std::filesystem::path GameBackupPath(const std::filesystem::path& outputDirectory, const std::string& projectName)
{
    return BackupDirectoryFor(outputDirectory) / (projectName + "_Game.dll.bak");
}

std::filesystem::path EditorBackupPath(const std::filesystem::path& outputDirectory, const std::string& projectName)
{
    return BackupDirectoryFor(outputDirectory) / (projectName + "_Editor.dll.bak");
}

} // namespace

bool BackupProjectAssemblyBinaries(const std::string& projectName, const std::filesystem::path& outputDirectory)
{
    const std::filesystem::path gameSource = GameDllPath(outputDirectory, projectName);
    if (!std::filesystem::exists(gameSource)) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "BackupProjectAssemblyBinaries('" + projectName + "') - no _Game.dll found at " + gameSource.string() +
            " - this is not a valid, currently-loaded Project Assembly to back up.");
        return false;
    }

    const std::filesystem::path backupDirectory = BackupDirectoryFor(outputDirectory);
    std::error_code createDirectoriesError;
    std::filesystem::create_directories(backupDirectory, createDirectoriesError);
    if (createDirectoriesError) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "BackupProjectAssemblyBinaries('" + projectName + "') - could not create backup directory " +
            backupDirectory.string() + " (" + createDirectoriesError.message() + ").");
        return false;
    }

    std::error_code copyError;
    std::filesystem::copy_file(gameSource, GameBackupPath(outputDirectory, projectName),
        std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "BackupProjectAssemblyBinaries('" + projectName + "') - failed to copy " + gameSource.string() +
            " (" + copyError.message() + ").");
        return false;
    }

    // _Editor.dll is OPTIONAL (a project may have no Editor sources - mirrors
    // RunBuildThreadBody()'s own existing "no Editor target is normal, not a
    // failure" handling) - only attempt this copy if it actually exists.
    const std::filesystem::path editorSource = EditorDllPath(outputDirectory, projectName);
    if (std::filesystem::exists(editorSource)) {
        std::error_code editorCopyError;
        std::filesystem::copy_file(editorSource, EditorBackupPath(outputDirectory, projectName),
            std::filesystem::copy_options::overwrite_existing, editorCopyError);
        if (editorCopyError) {
            GTE_LOG_ERROR("ProjectAssemblyBuild",
                "BackupProjectAssemblyBinaries('" + projectName + "') - failed to copy " + editorSource.string() +
                " (" + editorCopyError.message() + ").");
            return false;
        }
    }

    GTE_LOG_INFO("ProjectAssemblyBuild", "BackupProjectAssemblyBinaries('" + projectName + "') succeeded.");
    return true;
}

bool RestoreProjectAssemblyBinariesFromBackup(const std::string& projectName, const std::filesystem::path& outputDirectory)
{
    const std::filesystem::path gameBackup = GameBackupPath(outputDirectory, projectName);
    if (!std::filesystem::exists(gameBackup)) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "RestoreProjectAssemblyBinariesFromBackup('" + projectName + "') - no backup found at " +
            gameBackup.string() + ".");
        return false;
    }

    std::error_code copyError;
    std::filesystem::copy_file(gameBackup, GameDllPath(outputDirectory, projectName),
        std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "RestoreProjectAssemblyBinariesFromBackup('" + projectName + "') - failed to restore " +
            gameBackup.string() + " (" + copyError.message() + ").");
        return false;
    }

    // _Editor.dll's backup is OPTIONAL - only restore it if a backup of it
    // was actually ever taken (mirrors BackupProjectAssemblyBinaries()'s own
    // identical "_Editor.dll is optional" handling).
    const std::filesystem::path editorBackup = EditorBackupPath(outputDirectory, projectName);
    if (std::filesystem::exists(editorBackup)) {
        std::error_code editorCopyError;
        std::filesystem::copy_file(editorBackup, EditorDllPath(outputDirectory, projectName),
            std::filesystem::copy_options::overwrite_existing, editorCopyError);
        if (editorCopyError) {
            GTE_LOG_ERROR("ProjectAssemblyBuild",
                "RestoreProjectAssemblyBinariesFromBackup('" + projectName + "') - failed to restore " +
                editorBackup.string() + " (" + editorCopyError.message() + ").");
            return false;
        }
    }

    GTE_LOG_INFO("ProjectAssemblyBuild", "RestoreProjectAssemblyBinariesFromBackup('" + projectName + "') succeeded.");
    return true;
}

} // namespace gte
