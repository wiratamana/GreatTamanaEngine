#include "JobsPanel.h"

#include "../EditorFrame.h"
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

// Pixel height of one worker row and the gap between rows.
constexpr float kRowHeight = 24.0f;
constexpr float kRowSpacing = 4.0f;

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
        ImGui::SetTooltip("%s\n\nFor a fair comparison, compare the same model/animation/frame in both modes.",
            SkinningModeCrossReferenceHint(isGpuMode));
    }
}

void BuildPauseControl(Profiling::FrameProfiler& profiler)
{
    if (!profiler.IsCaptureEnabled()) {
        ImGui::TextDisabled("Capture disabled (see \"Profiler\" panel) - no new frames are being recorded.");
        ImGui::SameLine();
    }

    bool paused = profiler.IsDisplayPaused();
    if (ImGui::Checkbox("Pause", &paused)) {
        profiler.SetDisplayPaused(paused);
    }
}

void BuildUtilizationSummary(const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t workerCount)
{
    const WorkerUtilizationSummary summary = ComputeWorkerUtilizationSummary(points, workerCount);
    ImGui::TextUnformatted(FormatWorkerUtilizationSummary(summary).c_str());
}

// Packs a JobColor (0..1 floats) into ImGui's ImU32.
ImU32 PackColor(const JobColor& color, unsigned char alpha = 255)
{
    return IM_COL32(static_cast<int>(color.r * 255.0f), static_cast<int>(color.g * 255.0f),
        static_cast<int>(color.b * 255.0f), alpha);
}

void BuildTimeline(
    const std::vector<Profiling::WorkerTimelinePoint>& points, double frameMilliseconds, std::size_t workerCount)
{
    ImGui::SeparatorText("Timeline");

    if (points.empty()) {
        ImGui::TextDisabled("%s", JobsTimelineEmptyMessage());
    }

    // Horizontal scale. Falls back to the last job end when the frame time is not known yet.
    double frameDurationMs = frameMilliseconds;
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

        drawList->AddRectFilled(origin, rowEnd, IM_COL32(60, 60, 60, 255));
        drawList->AddRect(origin, rowEnd, IM_COL32(90, 90, 90, 255));

        const std::vector<Profiling::WorkerTimelinePoint> rowPoints = PointsForWorker(points, workerIndex);
        for (const Profiling::WorkerTimelinePoint& point : rowPoints) {
            const double startFrac = std::clamp(point.startMilliseconds / frameDurationMs, 0.0, 1.0);
            const double endFrac
                = std::clamp((point.startMilliseconds + point.durationMilliseconds) / frameDurationMs, 0.0, 1.0);

            const float segStartX = origin.x + static_cast<float>(startFrac) * rowWidth;
            // At least 2 px wide so a very short job stays visible.
            const float segEndX = std::max(origin.x + static_cast<float>(endFrac) * rowWidth, segStartX + 2.0f);

            const JobColor color = ColorForJobName(point.name);
            drawList->AddRectFilled(ImVec2(segStartX, origin.y + 1.0f), ImVec2(segEndX, rowEnd.y - 1.0f), PackColor(color));

            if (ImGui::IsMouseHoveringRect(ImVec2(segStartX, origin.y), ImVec2(segEndX, rowEnd.y))) {
                ImGui::SetTooltip(
                    "%s\n%.3f ms", point.name != nullptr ? point.name : "(unnamed)", point.durationMilliseconds);
            }
        }

        drawList->AddText(ImVec2(origin.x + 4.0f, origin.y + 4.0f), IM_COL32(230, 230, 230, 255), label);

        ImGui::Dummy(ImVec2(rowWidth, kRowHeight));
        ImGui::Dummy(ImVec2(rowWidth, kRowSpacing));
    }
}

} // namespace

void JobsPanel::SyncPauseEdge()
{
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    const bool paused = profiler.IsDisplayPaused();
    if (paused && !m_wasPaused) {
        m_frozenPoints = Profiling::BuildWorkerTimelinePoints(profiler.LastCompletedFrame());
        m_frozenFrameMilliseconds = profiler.LastCompletedFrame().summary.cpuMilliseconds;
    }
    m_wasPaused = paused;
}

void JobsPanel::OnFrame(EditorFrame& /*frame*/)
{
    SyncPauseEdge();
}

void JobsPanel::OnGui(EditorFrame& frame)
{
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    const std::size_t workerCount = Jobs::JobSystem::Instance().WorkerCount();

    BuildSkinningModeControl(frame.game);
    ImGui::Separator();

    BuildPauseControl(profiler);
    SyncPauseEdge();
    const bool paused = profiler.IsDisplayPaused();

    std::vector<Profiling::WorkerTimelinePoint> livePoints;
    const std::vector<Profiling::WorkerTimelinePoint>* points = &m_frozenPoints;
    double frameMilliseconds = m_frozenFrameMilliseconds;
    if (!paused) {
        livePoints = Profiling::BuildWorkerTimelinePoints(profiler.LastCompletedFrame());
        points = &livePoints;
        frameMilliseconds = profiler.LastCompletedFrame().summary.cpuMilliseconds;
    }

    BuildUtilizationSummary(*points, workerCount);
    BuildTimeline(*points, frameMilliseconds, workerCount);
}

} // namespace gte
