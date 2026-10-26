#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace gte {
class Renderer;
} // namespace gte

namespace gte::rg {

struct PassContext;

// Opens the renderer's draw recording for one graph pass and closes it on scope exit.
// Mesh draws (DrawScene, SubmitIndirect, ...) are only legal while one is alive.
class SceneRecording {
public:
    explicit SceneRecording(PassContext& ctx);
    ~SceneRecording();

    SceneRecording(const SceneRecording&) = delete;
    SceneRecording& operator=(const SceneRecording&) = delete;
    SceneRecording(SceneRecording&&) = delete;
    SceneRecording& operator=(SceneRecording&&) = delete;

private:
    Renderer* m_renderer = nullptr;
};

// Attachments of a scene redraw pass, as plain data. A clear value set clears, unset loads.
struct MeshPassRecord {
    TextureHandle color;
    TextureHandle depth; // May be the same texture as `color` (a view target carries both).
    std::optional<std::array<float, 4>> clearColor;
    std::optional<float> clearDepth;
};

// Why the record cannot be declared, or nullopt: at least one attachment is required.
std::optional<std::string> ValidateMeshPassRecord(const MeshPassRecord& record);

// Declares the attachment writes of a validated record.
void DeclareMeshPass(RenderGraphBuilder::PassBuilder& pass, const MeshPassRecord& record);

// A scene redraw into the given attachments. `draw` runs inside a SceneRecording; it may capture by value only.
//   MeshPass(builder, "Depth.Draw", scope, RenderPassEvent::PreOpaques).Depth(map, 1.0f).Draw(draw);
class MeshPass {
public:
    MeshPass(RenderGraphBuilder& builder, StaticName name, ViewScope scope, RenderPassEvent event);
    ~MeshPass();

    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;
    MeshPass(MeshPass&&) = delete;
    MeshPass& operator=(MeshPass&&) = delete;

    MeshPass& Color(TextureHandle target, std::optional<std::array<float, 4>> clear = std::nullopt);
    MeshPass& Depth(TextureHandle target, std::optional<float> clear = std::nullopt);

    // Declares the pass. False (and one error in the log) when it has no attachment or was declared twice.
    bool Draw(std::function<void()> draw);

private:
    void Report(const std::string& text);

    RenderGraphBuilder& m_builder;
    StaticName m_name;
    ViewScope m_scope;
    RenderPassEvent m_event;
    MeshPassRecord m_record;
    bool m_drawn = false;
};

} // namespace gte::rg
