// src/Core/Plugins/ProjectAssemblyBuildRunner.cpp
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE6.
// See ProjectAssemblyBuildRunner.h's own header comment for the full design
// rationale (background-thread choice, shutdown-behavior decision).
#include "ProjectAssemblyBuildRunner.h"

#include "../Logging.h"
#include "../../Jobs/JobSystem.h"

#include <windows.h>

#include <atomic>
#include <cctype>
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

// Runs ONE `cmake --build <buildDirectory> --target <targetName>`
// invocation as a real Windows child process (CreateProcessW(), per this
// phase's own doc file), redirecting its combined stdout/stderr to an
// anonymous pipe and reading that pipe, line by line, on the SAME thread
// that calls this function - this thread's entire job IS this blocking
// read loop (ReadFile() blocking is fine here, see this phase's own doc
// file). Every completed line is forwarded to GTE_LOG_INFO/WARNING/ERROR
// via a simple, best-effort, explicitly NOT-guaranteed-correct keyword
// heuristic (never parsed further than that). Returns the child's real
// exit code, or a negative sentinel if the process itself could not even
// be created/piped. `targetMissingHeuristicHit` is set true if the output
// looks like Ninja/CMake reporting that `targetName` simply does not exist
// - the caller uses this to distinguish "no Editor/ sources, perfectly
// normal" from a genuine build failure (this phase's own "STEP 2" doc-file
// reasoning).
int RunOneBuildTarget(const std::string& buildDirectory, const std::string& targetName, bool& targetMissingHeuristicHit)
{
    targetMissingHeuristicHit = false;

    SECURITY_ATTRIBUTES pipeSecurityAttributes{};
    pipeSecurityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    pipeSecurityAttributes.bInheritHandle = TRUE;
    pipeSecurityAttributes.lpSecurityDescriptor = nullptr;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &pipeSecurityAttributes, 0)) {
        GTE_LOG_ERROR("ProjectAssemblyBuild", "CreatePipe() failed - cannot capture 'cmake --build' output for target " + targetName + ".");
        return -1;
    }
    // The parent's own read end must never be inherited by the child - only
    // the write end (handed to the child as its stdout/stderr) is meant to
    // be inherited.
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    const std::wstring buildDirectoryWide = Utf8ToWide(buildDirectory);
    // QuoteWindowsArgument() (above) - NOT a naive "\"...\"" wrap - see that
    // function's own doc comment for the exact bug this avoids (a trailing
    // backslash, always present on buildDirectory per SDL_GetBasePath()'s
    // convention, would otherwise escape the closing quote).
    std::wstring commandLine = L"cmake --build " + QuoteWindowsArgument(buildDirectoryWide)
        + L" --target " + QuoteWindowsArgument(Utf8ToWide(targetName));
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = nullptr;

    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        nullptr, mutableCommandLine.data(), nullptr, nullptr, /*bInheritHandles=*/TRUE,
        CREATE_NO_WINDOW, nullptr, buildDirectoryWide.c_str(), &startupInfo, &processInfo);

    // The parent's own copy of the write end must be closed right after
    // CreateProcessW(), regardless of success/failure - otherwise ReadFile()
    // below would never see end-of-pipe (it would think the write end is
    // still open, via this leftover parent handle, even after the child
    // itself exits).
    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "CreateProcessW() failed (GetLastError=" + std::to_string(GetLastError()) + ") for target " + targetName + ".");
        return -1;
    }

    std::string lineBuffer;
    char readBuffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, readBuffer, sizeof(readBuffer), &bytesRead, nullptr) && bytesRead > 0) {
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
            if (lower.find("unknown target") != std::string::npos
                || lower.find("no rule to make target") != std::string::npos
                || lower.find("targets not built") != std::string::npos) {
                targetMissingHeuristicHit = true;
                GTE_LOG_WARNING("ProjectAssemblyBuild", line);
            } else if (lower.find("error") != std::string::npos) {
                GTE_LOG_ERROR("ProjectAssemblyBuild", line);
            } else if (lower.find("warning") != std::string::npos) {
                GTE_LOG_WARNING("ProjectAssemblyBuild", line);
            } else {
                GTE_LOG_INFO("ProjectAssemblyBuild", line);
            }
        }
    }
    if (!lineBuffer.empty()) {
        GTE_LOG_INFO("ProjectAssemblyBuild", lineBuffer);
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

// The actual background-thread body, registered via
// JobSystem::RegisterBackgroundThread() (never Schedule() - see this file's
// own header comment). Builds `<projectName>_Game` first; only attempts
// `<projectName>_Editor` if the `_Game` build succeeded, and treats a
// missing `_Editor` target (a project with no Editor/ sources - a
// perfectly normal, valid case per gte_add_project()'s own conditional
// target creation) as success, not failure - per this phase's own "STEP 2"
// doc-file instruction.
void RunBuildThreadBody(std::string projectName, std::string buildDirectory, std::shared_ptr<std::atomic<bool>> completionFlag)
{
    GTE_LOG_INFO("ProjectAssemblyBuild",
        "Starting build for Project Assembly '" + projectName + "' (build directory: " + buildDirectory + ")...");

    bool gameTargetMissing = false;
    const int gameExitCode = RunOneBuildTarget(buildDirectory, projectName + "_Game", gameTargetMissing);

    int editorExitCode = 0;
    if (gameExitCode == 0) {
        bool editorTargetMissing = false;
        editorExitCode = RunOneBuildTarget(buildDirectory, projectName + "_Editor", editorTargetMissing);
        if (editorExitCode != 0 && editorTargetMissing) {
            GTE_LOG_INFO("ProjectAssemblyBuild",
                "Project '" + projectName + "' has no '_Editor' target (no Editor/ sources) - this is normal, not a build failure.");
            editorExitCode = 0;
        }
    } else {
        GTE_LOG_ERROR("ProjectAssemblyBuild",
            "'" + projectName + "_Game' target build failed (exit code " + std::to_string(gameExitCode) + ") - skipping the '_Editor' target attempt.");
    }

    const int finalExitCode = (gameExitCode != 0) ? gameExitCode : editorExitCode;
    // This exact reminder MUST appear in the final log line, every time
    // (success or failure) - see this phase's own doc file, and LDD4
    // (Project Assemblies are never hot-reloaded).
    GTE_LOG_INFO("ProjectAssemblyBuild",
        "Build finished with exit code " + std::to_string(finalExitCode) +
        " - relaunch GreatTamanaEditor.exe to use the result (Project Assemblies are not hot-reloaded, see PHASE0_MASTER_STRATEGY.md, LDD4).");

    ClearInFlight(projectName);
    // Set right before returning, per JobSystem::RegisterBackgroundThread()'s
    // own documented contract (mirrors JobContinuation.cpp's identical
    // precedent).
    completionFlag->store(true, std::memory_order_release);
}

} // namespace

void TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory)
{
    if (!TryMarkInFlight(projectName)) {
        GTE_LOG_WARNING("ProjectAssemblyBuild",
            "A build for Project Assembly '" + projectName + "' is already in progress - ignoring this new request.");
        return;
    }

    auto completionFlag = std::make_shared<std::atomic<bool>>(false);
    std::thread buildThread(&RunBuildThreadBody, projectName, buildDirectory, completionFlag);
    // JobSystem::RegisterBackgroundThread() - NEVER JobSystem::Schedule(),
    // which runs on a FIXED-size worker pool shared by every other engine
    // subsystem - see this file's own header comment for the full
    // reasoning, and JobContinuation.cpp's WatchDependencyWithFallback()
    // for the real, working precedent this mirrors.
    gte::Jobs::JobSystem::Instance().RegisterBackgroundThread(std::move(buildThread), completionFlag);
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

} // namespace gte
