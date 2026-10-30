#include "ProfilerScopeCatalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace gte {
namespace {

using enum ScopeCategory;
using Profiling::ScopeLane;
using Profiling::ScopeOrigin;

struct Entry {
    std::string_view name;
    std::string_view title;
    std::string_view explanation;
    std::string_view hint;
    ScopeCategory category;
    bool waiting;
};

constexpr std::string_view kEngineFallback = "Engine code. No description yet.";
constexpr std::string_view kProjectFallback =
    "Code from your project. The time includes everything this function calls.";

// Unsorted source rows; the lookup table below is a sorted copy.
constexpr auto kRaw = std::to_array<Entry>({
    // Main thread, editor host.
    { "EditorHost::PollEvents", "Reading input", "Collects keyboard, mouse and window events from Windows.", "", Input, false },
    { "EditorHost::ExecuteEngineCommand", "Running a remote command", "Runs one command that came in through the HTTP debug server.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteAssetImportCommand", "Importing an asset", "Imports a file (mesh, texture, motion) into the project.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteEditorUiCommand", "Running an editor command", "Runs one editor command that came in through the HTTP debug server.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteFeatureDebugCommand", "Changing a feature setting", "Applies a setting change to a render feature.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteFrameDebuggerCommand", "Frame debugger command", "Handles one request of the frame debugger.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteHizDemoMenuAction", "Hi-Z demo menu action", "Opens or closes the Hi-Z demo scene.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteProjectLifecycleLoad", "Loading a project", "Loads the code of a game project into the editor.", "", LoadingAndTools, false },
    { "EditorHost::ExecuteRenderGraphControlCommand", "Render graph command", "Switches a render pass or feature on or off.", "", LoadingAndTools, false },
    { "EditorHost::PerformProjectAssemblyHotReload", "Hot reload of project code", "Rebuilds and reloads your project code. The editor is frozen while this runs.", "", LoadingAndTools, false },

    // Simulation.
    { "Game::Update", "Game update", "Runs one step of the simulation: animation, physics and game systems.", "", Simulation, false },
    { "AnimationSystem::EvaluatePoses", "Animation poses", "Works out where every bone of every animated model is this frame.", "", Simulation, false },
    { "PhysicsSystem::Update", "Physics", "Moves physical objects and chains and handles collisions.", "", Simulation, false },
    { "AnimationSystem::SkinAndUpload", "Animation skinning and upload", "Bends animated meshes to their pose and sends the new vertices to the graphics card.", "", Simulation, false },

    // Rendering, CPU side.
    { "Render.Prepare", "Preparing to draw", "Frees graphics resources that nobody uses and picks the pictures this frame draws into.", "", RenderPrepare, false },
    { "Offscreen", "Drawing the 3D views (CPU side)", "The CPU prepares and sends all the work that draws the Scene and Game views into pictures. It includes the time the CPU waits for the graphics card.", "If this is big, look at its children. A big \"waiting\" child means the graphics card is the limit.", RenderPrepare, false },
    { "Offscreen.Begin", "Start of drawing", "Waits until the graphics card has finished the previous frame, then opens a new list of commands.", "", RenderPrepare, false },
    { "Offscreen.Declare", "Planning the drawing", "Builds the list of render passes (steps) that the graphics card must run this frame.", "If this is big, a render feature or the object list takes long to plan.", RenderPrepare, false },
    { "Offscreen.Collect", "Finding what to draw", "Gathers the skinning jobs and the list of object clusters that may be drawn.", "", RenderPrepare, false },
    { "Offscreen.Compile", "Sorting the plan", "Puts the render passes in order and removes the ones nobody needs.", "", RenderPrepare, false },
    { "Offscreen.Record", "Writing graphics commands", "The CPU writes the draw commands of every render pass into the command list.", "If this is big, a pass has too many draw calls.", RenderRecord, false },
    { "Offscreen.Finish", "Closing the plan", "Cleans up and saves debug information after the commands are written.", "", RenderPrepare, false },
    { "Offscreen.FinalizeViews", "Making the views readable", "Switches the finished pictures so the editor can show them.", "", RenderPrepare, false },
    { "Offscreen.Submit", "Sending work to the graphics card", "Sends the commands to the GPU and waits for it to finish. A big number here means the GPU is slow.", "Draw less: fewer objects, lower resolution, fewer effects.", Waiting, false },
    { "Offscreen.Finalize", "End of drawing", "Last bookkeeping after the graphics card has finished.", "", RenderPrepare, false },
    { "Present", "Showing the picture (CPU side)", "Copies the finished frame into the window and presents it.", "", RenderPrepare, false },
    { "Present.Declare", "Planning the final copy", "Builds the short plan that copies the picture to the window.", "", RenderPrepare, false },
    { "Present.Compile", "Sorting the final plan", "Orders the steps of the final copy.", "", RenderPrepare, false },
    { "Present.Record", "Writing the final copy", "Writes the commands that copy the picture to the window.", "", RenderRecord, false },
    { "Present.Finish", "Closing the final copy", "Cleans up after the final copy.", "", RenderPrepare, false },
    { "RenderSystem::CollectRenderables", "Listing visible objects", "Walks the scene and lists every mesh that may be drawn.", "If this is big, the scene has very many objects.", RenderPrepare, false },
    { "RenderSystem::Draw", "Recording object draws", "Writes the draw commands for the objects in the scene.", "", RenderRecord, false },
    { "ClusterPart.Bounds", "Cluster boxes", "Works out the bounding box of every cluster instance. Helper threads share this work.", "", RenderPrepare, false },
    { "ClusterPart.Skip", "Skipping hidden clusters", "Finds mesh groups that are completely outside the camera view and skips them.", "", RenderPrepare, false },
    { "ClusterPart.Claim", "Handing objects to GPU drawing", "Tells the normal CPU draw which objects the GPU cluster path already draws.", "", RenderPrepare, false },

    // Editor panels.
    { "IEditorLayer::BuildUI", "Editor user interface", "Builds all editor panels for this frame. It is not part of the game.", "", EditorUi, false },
    { "HierarchyPanel", "Hierarchy panel", "Draws the list of objects in the scene.", "", EditorUi, false },
    { "Panel.DockspaceAndMenuBar", "Menu bar and layout", "Draws the menu bar and the panel layout.", "", EditorUi, false },
    { "Panel.ProjectWindows", "Project windows", "Draws windows that belong to project code.", "", EditorUi, false },
    { "Panel.ViewDescriptors", "View pictures", "Prepares the pictures shown in the editor views.", "", EditorUi, false },
    { "Panel.Inspector", "Inspector panel", "Draws the properties of the selected object.", "", EditorUi, false },
    { "Panel.SceneView", "Scene view panel", "Shows the scene view picture and its tools.", "", EditorUi, false },
    { "Panel.GameView", "Game view panel", "Shows the game view picture.", "", EditorUi, false },
    { "Panel.Memory", "Memory panel", "Shows how much graphics memory is used.", "", EditorUi, false },
    { "Panel.Profiler", "Profiler panel", "Draws this profiler.", "", EditorUi, false },
    { "Panel.RenderGraph", "Render graph panel", "Shows the render passes and how they connect.", "", EditorUi, false },
    { "Panel.RenderFeatures", "Render features panel", "Lists the render features and their settings.", "", EditorUi, false },
    { "Panel.Jobs", "Jobs panel", "Shows work on the helper threads.", "", EditorUi, false },
    { "Panel.Log", "Log panel", "Shows log messages.", "", EditorUi, false },
    { "Panel.FrameDebugger", "Frame debugger panel", "Lets you look at one frame draw call by draw call.", "", EditorUi, false },
    { "Panel.Project", "Project panel", "Shows the files of the project.", "", EditorUi, false },
    { "Panel.BoneViewer", "Bone viewer", "Shows the bones of a model.", "", EditorUi, false },
    { "Panel.ClusterViewer", "Cluster viewer", "Shows the clusters of a mesh.", "", EditorUi, false },
    { "Panel.SceneCameraAdapter", "Scene camera control", "Handles the camera of the scene view.", "", EditorUi, false },
    { "Panel.Plugins", "Project panels", "Draws editor panels added by project code.", "", EditorUi, false },

    // Loading and tools.
    { "Project.StartScan", "Starting a project folder scan", "Asks a helper thread to look at the project files.", "", LoadingAndTools, false },
    { "Project.ApplyScan", "Showing project files", "Takes the finished project file list and shows it.", "", LoadingAndTools, false },
    { "Scene.Load", "Loading a scene", "Reads a scene file and builds its objects.", "", LoadingAndTools, false },
    { "Scene.Save", "Saving a scene", "Writes the scene to a file.", "", LoadingAndTools, false },
    { "Scene.Destroy", "Clearing a scene", "Removes all objects of the scene.", "", LoadingAndTools, false },

    // Pure waits.
    { "Wait.Workers", "Waiting for helper threads", "The main thread is idle until background jobs finish.", "If this is big, the jobs are too large or there are too few helper threads.", Waiting, true },
    { "Wait.TargetResize", "Waiting while resizing", "Waits for the graphics card before a picture changes size.", "", Waiting, true },
    { "Wait.OneShotSubmit", "Waiting for a one-time graphics job", "A tool needs a result from the graphics card right now.", "", Waiting, true },

    // Helper threads (job scopes).
    { "SkinVertices", "Skinning vertices", "Bends mesh vertices to the animation pose on a helper thread.", "", Simulation, false },
    { "PackMeshVertices", "Packing vertices", "Converts mesh vertices to the format the graphics card reads.", "", Simulation, false },
    { "PackMeshVertexUvs", "Packing texture coordinates", "Converts texture coordinates to the format the graphics card reads.", "", Simulation, false },
    { "StepDynamicChain", "Physics chain step", "Moves one chain of springy bones.", "", Simulation, false },
    { "MeshClusterBuild", "Building clusters", "Cuts a mesh into small clusters for fast drawing.", "", LoadingAndTools, false },
    { "Project.EnsureRoot", "Checking the project folder", "Makes sure the project folder exists.", "", LoadingAndTools, false },
    { "Project.ScanTree", "Scanning project files", "Reads the list of files in the project folder.", "", LoadingAndTools, false },
    { "Project.LinkedFolder", "Scanning the code folder", "Reads the files of the active code project.", "", LoadingAndTools, false },
});

constexpr auto SortedTable()
{
    auto table = kRaw;
    std::sort(table.begin(), table.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return table;
}

constexpr bool HasNoDuplicates(const std::array<Entry, kRaw.size()>& table)
{
    for (std::size_t i = 1; i < table.size(); ++i) {
        if (table[i - 1].name == table[i].name) {
            return false;
        }
    }
    return true;
}

constexpr auto kTable = SortedTable();
static_assert(HasNoDuplicates(kTable), "duplicate scope name in the catalog table");

struct WaitSite {
    std::string_view site;
    std::string_view plain;
    std::string_view hint;
};

constexpr std::string_view kGpuLimitHint = "The graphics card is the limit. Draw less or lower the quality.";
constexpr std::string_view kVsyncHint =
    "The screen refresh (vsync) is the limit. Start with --no-vsync to measure real speed.";

constexpr auto kWaitSites = std::to_array<WaitSite>({
    { "PreviousOffscreen", "the previous frame is still being drawn", kGpuLimitHint },
    { "Offscreen", "this frame's drawing work is running on the graphics card", kGpuLimitHint },
    { "PresentInFlight", "the previous picture is still being shown", kVsyncHint },
    { "AcquireImage", "a free window picture is not ready yet", kVsyncHint },
    { "PresentQueue", "the picture is queued for the screen", kVsyncHint },
    { "Immediate" "Submit", "a one-time graphics job is running", "" }, // split: the leak scan matches the whole token
    { "WarmUp", "the engine is preparing graphics objects at start", "" },
    { "SwapchainRecreate", "the window picture chain is being rebuilt", "" },
    { "TargetResize", "a picture is changing size", "" },
    { "CaptureReadback", "a screenshot or debug picture is being copied back", "" },
    { "ResourceRelease", "unused graphics memory is being freed", "" },
    { "CodeUnload", "project code is being unloaded", "" },
    { "Shutdown", "the engine is closing", "" },
    { "ImGuiBackend", "the user interface renderer is waiting", "" },
});

ScopeInfo FromEntry(const Entry& entry)
{
    return { std::string(entry.title), std::string(entry.explanation), std::string(entry.hint), entry.category,
        entry.waiting };
}

const Entry* FindExact(std::string_view name)
{
    const auto it = std::lower_bound(
        kTable.begin(), kTable.end(), name, [](const Entry& entry, std::string_view key) { return entry.name < key; });
    return (it != kTable.end() && it->name == name) ? &*it : nullptr;
}

// Returns the text after `prefix`, or false when `name` does not start with it or nothing follows.
bool AfterPrefix(std::string_view name, std::string_view prefix, std::string_view& rest)
{
    if (!name.starts_with(prefix) || name.size() == prefix.size()) {
        return false;
    }
    rest = name.substr(prefix.size());
    return true;
}

ScopeInfo DescribeGpuWait(std::string_view inner)
{
    const std::size_t colon = inner.find(':');
    const std::string_view site = inner.substr(0, colon);
    const std::string_view detail = colon == std::string_view::npos ? std::string_view{} : inner.substr(colon + 1);

    std::string_view plain = "the graphics card is busy";
    std::string_view hint;
    for (const WaitSite& entry : kWaitSites) {
        if (entry.site == site) {
            plain = entry.plain;
            hint = entry.hint;
            break;
        }
    }

    ScopeInfo info;
    info.title = "Waiting for the graphics card: " + std::string(plain);
    if (!detail.empty()) {
        info.title += " (" + std::string(detail) + ")";
    }
    info.explanation = "The CPU is idle and waits until " + std::string(plain) + ". This is not CPU work.";
    info.hint = std::string(hint);
    info.category = Waiting;
    info.waiting = true;
    return info;
}

ScopeInfo DescribeGpuPass(std::string_view name)
{
    ScopeInfo info;
    info.category = GpuWork;
    if (name == "(folded passes)") {
        info.title = "Small passes (grouped)";
        info.explanation =
            "Many small passes were measured together because the measuring budget ran out.";
        return info;
    }
    info.title = "\"" + std::string(name) + "\" on the GPU";
    info.explanation = "How long the graphics card spent on the render pass \"" + std::string(name)
        + "\". Waits for earlier passes are included.";
    info.hint = "If this pass is the biggest, make it draw less or run at a lower quality.";
    return info;
}

ScopeInfo DescribeByPrefix(std::string_view name, bool& found)
{
    found = true;
    std::string_view rest;
    ScopeInfo info;

    if (AfterPrefix(name, "GpuWait(", rest) && rest.ends_with(')')) {
        return DescribeGpuWait(rest.substr(0, rest.size() - 1));
    }
    if (AfterPrefix(name, "Pass.", rest)) {
        info.title = "Draw step \"" + std::string(rest) + "\" (CPU)";
        info.explanation = "The CPU writes the graphics commands of the render pass \"" + std::string(rest)
            + "\". Slow means the pass has many draws or heavy setup.";
        info.category = RenderRecord;
        return info;
    }
    if (AfterPrefix(name, "Declare.", rest)) {
        info.title = "Plan step \"" + std::string(rest) + "\"";
        info.explanation = "The CPU decides which render passes \"" + std::string(rest)
            + "\" adds for this frame. Slow means too many features or objects to plan.";
        info.category = RenderPrepare;
        return info;
    }
    if (AfterPrefix(name, "Feature.", rest)) {
        const std::size_t dot = rest.find('.');
        const std::string_view feature = rest.substr(0, dot);
        if (dot == std::string_view::npos || dot + 1 >= rest.size()) {
            info.title = "Render feature \"" + std::string(feature) + "\"";
            info.explanation = "CPU time this render feature needed to add its passes this frame.";
        } else {
            const std::string_view callback = rest.substr(dot + 1);
            info.title = "Render feature \"" + std::string(feature) + "\" (" + std::string(callback) + ")";
            info.explanation = "CPU time of the callback " + std::string(callback) + " of the render feature \""
                + std::string(feature) + "\".";
        }
        info.category = RenderFeature;
        return info;
    }
    if (AfterPrefix(name, "Panel.", rest)) {
        info.title = "Editor panel \"" + std::string(rest) + "\"";
        info.explanation = "Time to draw the editor panel \"" + std::string(rest) + "\". It is not part of the game.";
        info.category = EditorUi;
        return info;
    }
    found = false;
    return info;
}

ScopeInfo DescribeByOrigin(std::string_view name, ScopeOrigin origin)
{
    ScopeInfo info;
    info.title = name.empty() ? "(unnamed)" : std::string(name);
    info.generic = true;
    if (origin == ScopeOrigin::Project) {
        info.explanation = std::string(kProjectFallback);
        info.category = ProjectCode;
    } else {
        info.explanation = std::string(kEngineFallback);
        info.category = Other;
    }
    return info;
}

// Writes "%.1f" of a value into text form.
std::string OneDecimal(double value)
{
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.1f", value);
    return buffer.data();
}

const char* StatusWord(Profiling::SampleStatus status)
{
    using Profiling::SampleStatus;
    switch (status) {
    case SampleStatus::Present: return "present";
    case SampleStatus::Stale: return "stale";
    case SampleStatus::Absent: return "absent";
    case SampleStatus::Unsupported: return "unsupported";
    case SampleStatus::Partial: return "partial";
    case SampleStatus::Error: return "error";
    case SampleStatus::Inactive: return "inactive";
    }
    return "absent";
}

} // namespace

const char* CategoryLabel(ScopeCategory category)
{
    switch (category) {
    case Simulation: return "Simulation";
    case Input: return "Input";
    case RenderPrepare: return "Render prepare";
    case RenderRecord: return "Render recording";
    case RenderFeature: return "Render feature";
    case GpuWork: return "GPU work";
    case Waiting: return "Waiting";
    case EditorUi: return "Editor UI";
    case LoadingAndTools: return "Loading and tools";
    case ProjectCode: return "Your code";
    case Other: return "Other";
    }
    return "Other";
}

ScopeInfo DescribeScope(std::string_view name, ScopeLane lane, ScopeOrigin origin)
{
    if (lane == ScopeLane::Gpu) {
        return DescribeGpuPass(name);
    }
    if (const Entry* entry = FindExact(name)) {
        return FromEntry(*entry);
    }
    bool found = false;
    ScopeInfo info = DescribeByPrefix(name, found);
    if (found) {
        return info;
    }
    return DescribeByOrigin(name, origin);
}

ScopeInfo DescribeGpuFeature(std::string_view feature)
{
    ScopeInfo info;
    info.title = "Render feature \"" + std::string(feature) + "\" (GPU)";
    info.explanation = "How long the graphics card spent on all passes of the render feature \"" + std::string(feature) + "\".";
    info.category = GpuWork;
    return info;
}

const char* DescribeVerdict(Profiling::VerdictKind kind)
{
    using Profiling::VerdictKind;
    switch (kind) {
    case VerdictKind::GpuLimited:
        return "The graphics card (GPU) is the slowest part and the CPU mostly waits for it. To go faster, draw less: "
               "fewer objects, lower resolution, fewer effects. See the GPU rows to find the most expensive step.";
    case VerdictKind::CpuLimited:
        return "The processor (CPU) is the slowest part. Look at the biggest numbers in the Self column of the main "
               "thread.";
    case VerdictKind::PresentLimited:
        return "The frame waits for the screen (vsync). Start the editor with --no-vsync to measure the real speed.";
    case VerdictKind::Stalled:
        return "Something made the CPU wait for a long time that is not the graphics card and not the screen. Look at "
               "the biggest row marked (waiting).";
    case VerdictKind::Serial:
        return "The CPU and the GPU take turns instead of working at the same time. Both parts add up to the frame "
               "time.";
    case VerdictKind::Unreliable:
        return "The profiler does not trust its own numbers this frame. Read the Checks list first.";
    case VerdictKind::Unknown:
        return "Not enough data yet. Wait a few seconds.";
    }
    return "Not enough data yet. Wait a few seconds.";
}

std::string BuildPlainSummary(const PlainSummaryInput& input)
{
    if (!(input.frameMs > 0.0)) {
        return "No frame data yet.";
    }
    const long fps = std::lround(1000.0 / input.frameMs);
    std::string text = "This frame took " + OneDecimal(input.frameMs) + " ms (" + std::to_string(fps) + " FPS). ";
    text += "The CPU worked for " + OneDecimal(input.cpuBusyMs) + " ms and waited for " + OneDecimal(input.cpuWaitMs)
        + " ms. ";
    if (input.gpuStatus == Profiling::SampleStatus::Present) {
        text += "The GPU worked for " + OneDecimal(input.gpuMs) + " ms.";
    } else {
        text += std::string("GPU time is not available (") + StatusWord(input.gpuStatus) + ").";
    }
    return text;
}

} // namespace gte
