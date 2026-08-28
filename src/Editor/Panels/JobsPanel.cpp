#include "JobsPanel.h"

#include "../EditorContext.h"
#include "../JobsPanelData.h"
#include "../../Game/Animation/AnimationSystem.h"
#include "../../Game/Game.h"
#include "../../Jobs/JobSystem.h"
#include "../../Profiling/FrameProfiler.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace gte {

namespace {

// Fixed pixel height of one worker's own timeline row, and the vertical gap
// between rows - purely a Tier-2, ImGui-facing layout constant, same
// "reasonable fixed constant, not derived from anything" treatment
// ProfilerPanel.cpp's own kGraphWindowFrames gets.
constexpr float kRowHeight = 24.0f;
constexpr float kRowSpacing = 4.0f;

// --- Section 0: GPU Vertex Skinning mode toggle -----------------------------
// GPU Vertex Skinning campaign, Phase 7 (Editor Toggle & Profiling UX -
// task_manager/gpu_skinning/GPU_SKINNING_PHASE7_EDITOR_PROFILING_UX_STRATEGY_v1.md,
// Step 3.1/3.2). A single, obvious control flipping the engine-wide
// AnimationSystem::SkinningMode between CpuJobSystem and GpuCompute, wired
// straight into Game::SetSkinningMode()/GetSkinningMode() - no intermediate
// indirection layer, mirroring how ProfilerPanel's own Capture/Pause
// controls call straight into FrameProfiler/Renderer methods.
void BuildSkinningModeControl(Game& game)
{
    ImGui::SeparatorText("GPU Vertex Skinning");

    const bool isGpuMode = game.GetSkinningMode() == AnimationSystem::SkinningMode::GpuCompute;
    int modeIndex = isGpuMode ? 1 : 0;
    const char* labels[] = { SkinningModeDisplayName(false), SkinningModeDisplayName(true) };
    if (ImGui::Combo("Skinning Mode", &modeIndex, labels, 2)) {
        game.SetSkinningMode(
            modeIndex == 1 ? AnimationSystem::SkinningMode::GpuCompute : AnimationSystem::SkinningMode::CpuJobSystem);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        // A fair CPU-vs-GPU comparison needs the same model/frame/vertex
        // count in both modes - see this phase's own strategy document,
        // Step 3.4. This is a documentation-level responsibility (a
        // tooltip), not something enforced in code - see that step for why.
        ImGui::SetTooltip("%s\n\nFor a fair comparison, compare the same model/animation/frame in both modes.",
            SkinningModeCrossReferenceHint(isGpuMode));
    }
}

// --- Section 1: Pause control -----------------------------------------------

void BuildPauseControl(Profiling::FrameProfiler& profiler, bool& paused,
    std::vector<Profiling::WorkerTimelinePoint>& frozenPoints, Profiling::FrameSample& frozenLatestFrame)
{
    if (!profiler.IsCaptureEnabled()) {
        // Shares FrameProfiler's existing capture flag with "Profiler" (see
        // Panels/ProfilerPanel.cpp) - this panel has no own Capture
        // checkbox at all (see JobsPanel.h's own class comment), so it only
        // ever informs, never controls it.
        ImGui::TextDisabled("Capture disabled (see \"Profiler\" panel) - no new frames are being recorded at all.");
        ImGui::SameLine();
    }

    const bool wasPaused = paused;
    ImGui::Checkbox("Pause", &paused);

    // false -> true (pause just engaged): capture a frozen snapshot once,
    // this frame only - mirrors ProfilerPanel.cpp's own BuildCaptureAndPauseControls()
    // exactly (direction 2, staying true, and direction 3, true -> false,
    // both need no code here at all - every section below simply reads
    // `paused`'s current value).
    if (paused && !wasPaused) {
        frozenPoints = Profiling::BuildWorkerTimelinePoints(profiler.LastCompletedFrame());
        frozenLatestFrame = profiler.LastCompletedFrame();
    }
}

// --- Section 2: worker utilization summary ----------------------------------

void BuildUtilizationSummary(const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t workerCount)
{
    const WorkerUtilizationSummary summary = ComputeWorkerUtilizationSummary(points, workerCount);
    ImGui::TextUnformatted(FormatWorkerUtilizationSummary(summary).c_str());
}

// --- Section 3: the per-worker timeline --------------------------------------

// Packs a JobColor (0..1 floats) into ImGui's own 0xAABBGGRR-order ImU32.
ImU32 PackColor(const JobColor& color, unsigned char alpha = 255)
{
    return IM_COL32(static_cast<int>(color.r * 255.0f), static_cast<int>(color.g * 255.0f),
        static_cast<int>(color.b * 255.0f), alpha);
}

void BuildTimeline(const std::vector<Profiling::WorkerTimelinePoint>& points, const Profiling::FrameSample& latestFrame,
    std::size_t workerCount)
{
    ImGui::SeparatorText("Timeline");

    if (points.empty()) {
        // Distinguishes "job timing instrumentation is compiled out of this
        // build entirely" from "genuinely no job samples recorded yet this
        // frame" (see JobsPanelData.h's JobsTimelineEmptyMessage() and
        // ProfilerPanelData.h's own CpuScopeTableEmptyMessage() precedent) -
        // still falls through to draw every worker row as entirely idle
        // below, since an idle row is itself meaningful information (and is
        // exactly what GTE_ENABLE_JOB_SYSTEM=OFF/no jobs-yet always looks
        // like - see JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md, Step 3.5).
        ImGui::TextDisabled("%s", JobsTimelineEmptyMessage());
    }

    // Frame duration used as the timeline's own horizontal scale - falls
    // back to the latest end-of-segment time (or a small positive default)
    // for a degenerate/zero cpuFrameMilliseconds (e.g. the very first frame
    // ever, before EndFrame() has recorded a real duration) rather than
    // dividing by zero below.
    double frameDurationMs = latestFrame.cpuFrameMilliseconds;
    if (frameDurationMs <= 0.0) {
        double latestEnd = 0.0;
        for (const Profiling::WorkerTimelinePoint& point : points) {
            latestEnd = std::max(latestEnd, point.startMilliseconds + point.durationMilliseconds);
        }
        frameDurationMs = latestEnd > 0.0 ? latestEnd : 1.0;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rowWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);

    for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        char label[32];
        std::snprintf(label, sizeof(label), "Worker %zu", workerIndex);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 rowEnd(origin.x + rowWidth, origin.y + kRowHeight);

        // Idle background for the whole row - drawn first, so every real
        // segment below paints over it.
        drawList->AddRectFilled(origin, rowEnd, IM_COL32(60, 60, 60, 255));
        drawList->AddRect(origin, rowEnd, IM_COL32(90, 90, 90, 255));

        const std::vector<Profiling::WorkerTimelinePoint> rowPoints = PointsForWorker(points, workerIndex);
        for (const Profiling::WorkerTimelinePoint& point : rowPoints) {
            const double startFrac = std::clamp(point.startMilliseconds / frameDurationMs, 0.0, 1.0);
            const double endFrac
                = std::clamp((point.startMilliseconds + point.durationMilliseconds) / frameDurationMs, 0.0, 1.0);

            const float segStartX = origin.x + static_cast<float>(startFrac) * rowWidth;
            // Every segment is at least a couple of pixels wide, even for a
            // vanishingly short job, so it stays visible/hoverable rather
            // than collapsing to a zero-width invisible sliver.
            const float segEndX = std::max(origin.x + static_cast<float>(endFrac) * rowWidth, segStartX + 2.0f);

            const JobColor color = ColorForJobName(point.name);
            drawList->AddRectFilled(ImVec2(segStartX, origin.y + 1.0f), ImVec2(segEndX, rowEnd.y - 1.0f), PackColor(color));

            if (ImGui::IsMouseHoveringRect(ImVec2(segStartX, origin.y), ImVec2(segEndX, rowEnd.y))) {
                ImGui::SetTooltip(
                    "%s\n%.3f ms", point.name != nullptr ? point.name : "(unnamed)", point.durationMilliseconds);
            }
        }

        // Row label, left-aligned, drawn last so it stays legible over the
        // idle background even when no colored segment is present.
        drawList->AddText(ImVec2(origin.x + 4.0f, origin.y + 4.0f), IM_COL32(230, 230, 230, 255), label);

        ImGui::Dummy(ImVec2(rowWidth, kRowHeight));
        ImGui::Dummy(ImVec2(rowWidth, kRowSpacing));
    }
}

} // namespace

void JobsPanel::Build(EditorContext& /*ctx*/, Game& game)
{
    ImGui::Begin("Jobs");

    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    const std::size_t workerCount = Jobs::JobSystem::Instance().WorkerCount();

    BuildSkinningModeControl(game);
    ImGui::Separator();

    BuildPauseControl(profiler, m_paused, m_frozenPoints, m_frozenLatestFrame);

    // Every section below reads either the frozen snapshot (Pause is on) or
    // fresh live data (Pause is off) - mirrors ProfilerPanel::Build()'s own
    // "livePoints only exists to give the live branch somewhere to own its
    // own reshape result" pattern exactly.
    std::vector<Profiling::WorkerTimelinePoint> livePoints;
    const std::vector<Profiling::WorkerTimelinePoint>* points = nullptr;
    const Profiling::FrameSample* latestFrame = nullptr;

    if (m_paused) {
        points = &m_frozenPoints;
        latestFrame = &m_frozenLatestFrame;
    } else {
        latestFrame = &profiler.LastCompletedFrame();
        livePoints = Profiling::BuildWorkerTimelinePoints(*latestFrame);
        points = &livePoints;
    }

    BuildUtilizationSummary(*points, workerCount);
    BuildTimeline(*points, *latestFrame, workerCount);

    ImGui::End();
}

} // namespace gte
