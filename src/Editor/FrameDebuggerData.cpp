#include "FrameDebuggerData.h"

#include <cstddef>
#include <cstdio>

namespace gte {

FrameDebuggerSnapshot BuildPlaceholderFrameDebuggerSnapshot()
{
    // Deliberately empty - see FrameDebuggerData.h's own top-of-file
    // comment and PHASE0's Locked Design Decision #2. Returning a
    // default-constructed FrameDebuggerSnapshot{} is intentional, not a
    // stub left unfinished - this IS the finished behavior for this
    // campaign.
    return FrameDebuggerSnapshot{};
}

std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return "0 of 0";
    }
    // 1-based display, matching the reference screenshot's own
    // "2117 of 2117" convention - currentEventIndex is a 0-based index
    // internally (see ClampSelectedEventIndex()'s own contract), so +1
    // here, once, is the single place that conversion happens.
    const int displayIndex = currentEventIndex < 0 ? 0 : (currentEventIndex + 1);
    return std::to_string(displayIndex) + " of " + std::to_string(totalEventCount);
}

int ClampSelectedEventIndex(int requested, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return -1;
    }
    if (requested < 0) {
        return 0;
    }
    if (requested >= totalEventCount) {
        return totalEventCount - 1;
    }
    return requested;
}

std::string FormatFrameHistoryLabel(int cursorIndex, int count)
{
    if (count <= 0) {
        return "Frame 0 of 0";
    }
    // 1-based display, matching FormatFrameStepperLabel()'s own convention -
    // a negative cursorIndex (defensive only; FrameDebuggerHistory never
    // actually produces one once count > 0 - see
    // ClampFrameDebuggerHistoryCursor()) displays as "Frame 1", not "Frame
    // 0", for the same "never below the first real display index" reason.
    const int displayIndex = cursorIndex < 0 ? 1 : (cursorIndex + 1);
    return "Frame " + std::to_string(displayIndex) + " of " + std::to_string(count);
}

namespace {

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndexRecursive(
    const std::vector<FrameDebuggerEventNode>& nodes, int eventIndex)
{
    for (const FrameDebuggerEventNode& node : nodes) {
        if (node.isDrawCall && node.eventIndex == eventIndex) {
            return node.details;
        }
        std::optional<FrameDebuggerEventDetails> found
            = FindEventDetailsByIndexRecursive(node.children, eventIndex);
        if (found.has_value()) {
            return found;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex)
{
    if (eventIndex < 0) {
        return std::nullopt;
    }
    return FindEventDetailsByIndexRecursive(snapshot.rootNodes, eventIndex);
}

namespace {

// Trims a fixed-precision formatted float down to the shortest
// "does not lose the value" representation - e.g. "1.000000" -> "1",
// "0.500000" -> "0.5" - matching the reference screenshot's own compact
// "(1, 1, 1, 1)" style rather than a fixed 6-decimal dump.
std::string FormatCompactFloat(float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return std::string(buffer);
}

} // namespace

std::string FormatVectorProperty(const FrameDebuggerVectorProperty& vector)
{
    return "(" + FormatCompactFloat(vector.x) + ", " + FormatCompactFloat(vector.y) + ", "
        + FormatCompactFloat(vector.z) + ", " + FormatCompactFloat(vector.w) + ")";
}

std::string FormatMatrixProperty(const FrameDebuggerMatrixProperty& matrix)
{
    std::string result;
    for (int row = 0; row < 4; ++row) {
        if (row > 0) {
            result += "\n";
        }
        for (int col = 0; col < 4; ++col) {
            if (col > 0) {
                result += " ";
            }
            result += FormatCompactFloat(matrix.values[static_cast<std::size_t>(row * 4 + col)]);
        }
    }
    return result;
}

namespace {

// PHASE2 - duplicated, real, hardcoded clear-color constant. Mirrors
// src/Application/RenderPasses.cpp's own kGameClearColor VALUE exactly
// (20/255, 20/255, 30/255, 1.0f - see that file's own top-of-file comment,
// which already establishes "duplicate a hardcoded engine constant with a
// comment documenting the value it must be kept in sync with" as an
// accepted pattern in this codebase, rather than a workaround). NOT
// re-exported/re-used directly from RenderPasses.cpp/.h here on purpose:
// RenderPasses.cpp's own copy is an anonymous-namespace, internal-linkage
// local, and src/Editor/ must never #include src/Application/ headers (see
// AGENTS.md's "Clean Architecture" - Application is the composition root
// that depends on Editor, never the reverse; exposing it via a NEW shared
// header was considered and rejected as unnecessary churn for one constant
// this campaign's own PHASE0 already established a "just duplicate it with
// a comment" precedent for). If RenderPasses.cpp's own kGameClearColor ever
// changes, update this constant to match.
constexpr float kFrameDebuggerGameClearColor[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };

// Finds the first pass in `passes` whose name exactly matches `name`, or
// nullptr if none does - a plain linear scan, exactly as cheap/simple as
// this once-per-captured-frame lookup needs to be.
const rg::RenderGraphPassSnapshot* FindPassByName(
    const std::vector<rg::RenderGraphPassSnapshot>& passes, const std::string& name)
{
    for (const rg::RenderGraphPassSnapshot& pass : passes) {
        if (pass.name == name) {
            return &pass;
        }
    }
    return nullptr;
}

// frame-debugger-5 campaign, PHASE2
// (PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md, Step 3.2) -
// small, exhaustive-switch row-label helpers for a compute-dispatch leaf's
// read/write rows, keyed by PHASE1's new real rg::ResourceKind (never
// guessed) - mirrors RenderGraphTypes.cpp's own IsWriteAccess()/ToString()
// "deliberately NO `default:` case" convention exactly, so a future fourth
// ResourceKind enumerator fails to compile here until updated.
const char* ReadRowLabelForKind(rg::ResourceKind kind)
{
    switch (kind) {
    case rg::ResourceKind::Texture:
        return "Read Texture";
    case rg::ResourceKind::Buffer:
        return "Read Buffer";
    case rg::ResourceKind::VolumeTexture:
        return "Read Volume Texture";
    }
    return "Read Texture";
}

const char* WriteRowLabelForKind(rg::ResourceKind kind)
{
    switch (kind) {
    case rg::ResourceKind::Texture:
        return "Write Texture";
    case rg::ResourceKind::Buffer:
        return "Write Buffer";
    case rg::ResourceKind::VolumeTexture:
        return "Write Volume Texture";
    }
    return "Write Texture";
}

// frame-debugger-5 campaign, PHASE2 - the ONE generic leaf builder for ANY
// real compute dispatch (RenderGraphPassSnapshot::isComputePass == true)
// that survived this frame, whatever its name - GPU Skinning, every
// atmosphere LUT pass, Aerial Perspective Composite, the Aerial
// Perspective Volume Debug-Slice pass, Compute Blur Validation, and any
// future compute pass this engine ever adds. Mirrors the OLD, now-DELETED
// BuildGpuSkinningLeaf()/BuildAerialPerspectiveCompositeLeaf()'s own
// "n/a (compute pass)" blend/Z/stencil convention exactly (a compute
// dispatch never issues a draw call), but is the SINGLE, UNIFIED
// replacement for both of those deleted, name-specific functions - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6.
FrameDebuggerEventNode BuildComputeDispatchLeaf(const rg::RenderGraphPassSnapshot& pass, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = pass.name; // The real, raw render-graph pass name - never fabricated/prettified.
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Compute Dispatch";
    // No separate "friendly label" - the real, raw pass name IS the
    // passName too, for every compute leaf uniformly (this is what makes
    // this function correct for a pass this file has never heard of
    // before - see PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md's
    // own Step 3.4 for the OPTIONAL cosmetic-label idea this deliberately
    // does NOT implement by default).
    details.passName = pass.name;
    // shaderName - honestly, this generic function has no way to know a
    // real compute pass's exact .comp shader FILE name (that fact used to
    // live only in the deleted, hand-written
    // BuildAerialPerspectiveCompositeLeaf()'s own hardcoded string) - the
    // real, raw pass NAME is used here instead, which is always true and
    // never fabricated.
    details.shaderName = pass.name;

    details.blendMode = "n/a (compute pass)";
    details.zClip = "n/a (compute pass)";
    details.zTest = "n/a (compute pass)";
    details.zWrite = "n/a (compute pass)";
    details.cull = "n/a (compute pass)";
    details.stencilRef = "n/a (compute pass)";
    details.stencilComp = "n/a (compute pass)";
    details.stencilPass = "n/a (compute pass)";
    details.stencilFail = "n/a (compute pass)";
    details.stencilZFail = "n/a (compute pass)";

    // textures - real read/write resource names, EACH LABELED BY ITS REAL
    // KIND (PHASE1's new readKinds/writeKinds) so a buffer write (e.g. GPU
    // Skinning's own output buffer) is never mislabeled as a texture.
    for (std::size_t i = 0; i < pass.readNames.size(); ++i) {
        FrameDebuggerTextureProperty texture;
        texture.name = ReadRowLabelForKind(i < pass.readKinds.size() ? pass.readKinds[i] : rg::ResourceKind::Texture);
        texture.valueLabel = pass.readNames[i];
        details.textures.push_back(std::move(texture));
    }
    for (std::size_t i = 0; i < pass.writeNames.size(); ++i) {
        FrameDebuggerTextureProperty texture;
        texture.name
            = WriteRowLabelForKind(i < pass.writeKinds.size() ? pass.writeKinds[i] : rg::ResourceKind::Texture);
        texture.valueLabel = pass.writeNames[i];
        details.textures.push_back(std::move(texture));
    }

    // vectors - real GPU timing (0.0ms whenever GPU timing is
    // Absent/Unsupported, exactly like every other GPU-timing consumer in
    // this engine today - see AGENTS.md's "Profiling" section). A compute
    // dispatch never issues a draw call, so PassGpuStats::drawStats is
    // deliberately never reported here (see RenderPasses.cpp's
    // AddGpuSkinningPasses()/DrawStats.h's own header comment).
    FrameDebuggerVectorProperty timing;
    timing.name = "GPU Time (ms)";
    timing.x = static_cast<float>(pass.stats.timing.milliseconds);
    details.vectors.push_back(timing);

    // matrices deliberately left empty - a compute dispatch never uses a
    // rasterizer-consumed camera matrix (mirrors the deleted
    // BuildGpuSkinningLeaf()/BuildAerialPerspectiveCompositeLeaf()'s own
    // identical convention).

    leaf.details = std::move(details);
    return leaf;
}

// Builds the real "GameView" pass LEAF node - see
// BuildRealFrameDebuggerSnapshot()'s own header-comment tree-shape
// description and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6.
FrameDebuggerEventNode BuildGameViewLeaf(
    const rg::RenderGraphPassSnapshot& gameViewPass, const FrameDebuggerCaptureContext& capture, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = "GameView";
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Draw Mesh";
    details.passName = "GameView";

    // shaderName - every DISTINCT real Pipeline debug name recorded this
    // frame, comma-joined (Locked Design Decision #6). Empty (never a
    // fabricated placeholder name) if capture recorded no draws at all this
    // frame - a genuinely empty Game View is a real, honest outcome.
    {
        std::string joined;
        const std::vector<std::string>& names = capture.PipelineDebugNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                joined += ", ";
            }
            joined += names[i];
        }
        details.shaderName = joined;
    }

    // textures - one row per DISTINCT real bound MaterialTexture debug name
    // - there is no per-slot "_MainTex"-style naming in this engine yet, so
    // `name` is a stable, generic label rather than an invented one (see
    // this phase's own Step 3.1).
    for (const std::string& textureName : capture.MaterialTextureDebugNames()) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Material Texture";
        texture.valueLabel = textureName;
        details.textures.push_back(std::move(texture));
    }

    // vectors - the real clear color, plus the real aggregate DrawStats
    // (draw-call count / triangle count) taken directly from
    // graphSnapshot's own "GameView" pass entry, exactly as this phase's
    // own Step 3.1 specifies (NOT re-derived from `capture.DrawCallCount()`
    // - the graph snapshot's own stats are the authoritative source other
    // panels, e.g. the "Render Graph" panel, already trust).
    {
        FrameDebuggerVectorProperty clearColor;
        clearColor.name = "Clear Color";
        clearColor.x = kFrameDebuggerGameClearColor[0];
        clearColor.y = kFrameDebuggerGameClearColor[1];
        clearColor.z = kFrameDebuggerGameClearColor[2];
        clearColor.w = kFrameDebuggerGameClearColor[3];
        details.vectors.push_back(clearColor);

        FrameDebuggerVectorProperty drawStats;
        drawStats.name = "Draw Stats (Calls, Tris)";
        drawStats.x = static_cast<float>(gameViewPass.stats.drawStats.drawCallCount);
        drawStats.y = static_cast<float>(gameViewPass.stats.drawStats.triangleCount);
        details.vectors.push_back(drawStats);
    }

    // matrices - the real view-projection matrix this pass actually
    // rendered with this frame. Mat4 is COLUMN-MAJOR storage
    // (columns[c][r] via operator()(row, col) - see Math/Mat4.h's own class
    // comment), but FrameDebuggerMatrixProperty::values is ROW-MAJOR
    // (values[row*4 + col] is row `row`, column `col` - see
    // FrameDebuggerData.h's own struct comment) - so this copies through
    // Mat4::operator()(row, col) element-by-element (which already accounts
    // for the column-major storage internally) rather than a raw memcpy of
    // Mat4::Data() (which is column-major and would silently transpose the
    // displayed matrix - see PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md's
    // own explicit warning about exactly this bug class).
    {
        FrameDebuggerMatrixProperty viewProjection;
        viewProjection.name = "ViewProjection";
        const Mat4& matrix = capture.LastViewProjection();
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                viewProjection.values[static_cast<std::size_t>(row * 4 + col)] = matrix(row, col);
            }
        }
        details.matrices.push_back(viewProjection);
    }

    // blend/Z/stencil rows - this engine's real, single, constant Pipeline
    // configuration (Locked Design Decision #6).
    {
        const FrameDebuggerStandardPipelineState pipelineState = DescribeStandardPipelineState();
        details.blendMode = pipelineState.blendMode;
        details.zClip = pipelineState.zClip;
        details.zTest = pipelineState.zTest;
        details.zWrite = pipelineState.zWrite;
        details.cull = pipelineState.cull;
        details.stencilRef = pipelineState.stencilRef;
        details.stencilComp = pipelineState.stencilComp;
        details.stencilPass = pipelineState.stencilPass;
        details.stencilFail = pipelineState.stencilFail;
        details.stencilZFail = pipelineState.stencilZFail;
    }

    leaf.details = std::move(details);
    return leaf;
}

} // namespace

FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture, const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo)
{
    const rg::RenderGraphPassSnapshot* gameViewPass = FindPassByName(graphSnapshot.passesInExecutionOrder, "GameView");
    if (gameViewPass == nullptr) {
        // Honest "no frame captured yet" empty result - see this function's
        // own header-comment contract and PHASE0's Locked Design Decision #2.
        return FrameDebuggerSnapshot{};
    }

    int nextEventIndex = 0;

    FrameDebuggerEventNode root;
    root.name = "Game View";
    root.isDrawCall = false;

    // frame-debugger-5 campaign, PHASE2 - THE generic replacement for the
    // former "GPU Skinning" group AND the former hardcoded
    // "AtmosphereAerialPerspectiveCompositePass" special case (see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6). Every real,
    // surviving compute pass this frame becomes one leaf here, in real
    // execution order, whatever its name - "GameView" itself is
    // structurally excluded (it is never isComputePass==true, since it's
    // declared via plain AddPass()/WriteColorAttachment(), never
    // AddComputePass()). SPLIT into a pre/post pair (Locked Design
    // Decision #8, v2 review finding) so a pass that genuinely ran BEFORE
    // "GameView" (e.g. GPU Skinning, every atmosphere LUT pass) is never
    // shown as if it ran after it.
    //
    // frame-debugger-6 campaign, PHASE2
    // (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md) - BOTH loops
    // below also now exclude any surviving compute pass whose PHASE1-stamped
    // `rg::ViewScope` is `SceneView` - this engine genuinely runs a SEPARATE
    // copy of several Atmosphere compute passes for the Editor's own
    // Scene-View camera (writing to "..._SceneView"-suffixed resources) in
    // addition to the Game-View ones, but both copies used to share the exact
    // same literal pass NAME (e.g. "AtmosphereSkyViewLutPass"), so this tree
    // used to show duplicate, indistinguishable leaves and even leak a
    // genuinely Scene-View-only debug tool ("ComputeBlurValidation") into a
    // tree that this feature's own permanent rule says is Game-View ONLY
    // (see this campaign's own PHASE0_MASTER_STRATEGY.md Section 0 and Locked
    // Design Decision #2). `Shared` (e.g.
    // the Transmittance/Multi-Scattering LUT passes, genuinely computed once
    // per frame, not once per view) and `GameView` both still pass through
    // unchanged - this is a structural, one-line boolean filter, never a
    // pass-name/resource-suffix string comparison.
    const int gameViewIndex = static_cast<int>(gameViewPass - graphSnapshot.passesInExecutionOrder.data());

    FrameDebuggerEventNode preGameViewGroup;
    preGameViewGroup.name = "Compute Dispatches (Pre-GameView)";
    preGameViewGroup.isDrawCall = false;

    FrameDebuggerEventNode postGameViewGroup;
    postGameViewGroup.name = "Compute Dispatches (Post-GameView)";
    postGameViewGroup.isDrawCall = false;

    // frame-debugger-5 campaign, PHASE2 - IMPORTANT ("nextEventIndex
    // ordering" fix vs. the phase document's own inline code sample): the
    // phase document's Step 3.3 literally showed ONE single loop over the
    // WHOLE passesInExecutionOrder range that routes each surviving compute
    // pass into whichever group it belongs to. Re-verified against this
    // function's own explicitly-documented invariant (and the phase
    // document's own immediately-following "IMPORTANT (nextEventIndex
    // ordering caveat)" prose, which says the pre-GameView loop must assign
    // its leaves' eventIndex values BEFORE "GameView" gets its own, and the
    // post-GameView loop must assign its leaves' eventIndex values AFTER):
    // a SINGLE loop over the whole array would assign eventIndex values to
    // POST-GameView passes BEFORE "GameView" itself ever gets one (since the
    // single loop finishes completely before BuildGameViewLeaf() is ever
    // called) - silently breaking the documented "pre < GameView < post"
    // monotonic ordering for any frame with a POST-GameView compute pass.
    // Caught by this phase's own new
    // EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView test
    // (and 2 others) actually failing against the doc's literal sample.
    // Fixed here by using TWO range-restricted loops instead - one over
    // [0, gameViewIndex) BEFORE "GameView"'s own leaf is built, one over
    // (gameViewIndex, size) AFTER - so eventIndex is genuinely monotonic
    // increasing in true chronological order, exactly as documented.
    for (int i = 0; i < gameViewIndex; ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // frame-debugger-6 campaign, PHASE2
        // (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md, Step 3.1) -
        // a pass whose PHASE1-stamped rg::ViewScope is SceneView is NEVER
        // genuinely part of the Game View's own render/compute chain, even if
        // it happens to survive this frame and sit before "GameView"'s own
        // index - e.g. a Scene-View-only copy of an Atmosphere LUT pass that
        // shares its literal pass NAME with the real Game-View instance (the
        // exact real-world collision that motivated this whole campaign - see
        // PHASE0_MASTER_STRATEGY.md Section 0). `Shared` and `GameView` both
        // still pass through unchanged.
        if (!pass.isComputePass || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            continue;
        }
        preGameViewGroup.children.push_back(BuildComputeDispatchLeaf(pass, nextEventIndex++));
    }

    // Only add either group at all if something real actually survived
    // this frame in THAT half - mirrors the deleted "GPU Skinning" group's
    // own "only add if at least one real pass actually matched" discipline,
    // applied independently to each half (it is entirely normal/expected
    // for only one half to have children on a given captured frame).
    if (!preGameViewGroup.children.empty()) {
        root.children.push_back(std::move(preGameViewGroup));
    }
    root.children.push_back(BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++));

    for (int i = gameViewIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // frame-debugger-6 campaign, PHASE2 - identical rule, symmetrically
        // applied to the post-GameView half (see the pre-GameView loop's own
        // comment above for the full rationale) - e.g. the real
        // "AtmosphereAerialPerspectiveCompositePass" duplicate scenario.
        if (!pass.isComputePass || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            continue;
        }
        postGameViewGroup.children.push_back(BuildComputeDispatchLeaf(pass, nextEventIndex++));
    }
    if (!postGameViewGroup.children.empty()) {
        root.children.push_back(std::move(postGameViewGroup));
    }

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(std::move(root));
    snapshot.totalEventCount = nextEventIndex;
    snapshot.renderTarget = gameViewRenderTargetInfo;
    snapshot.renderTarget.name = "GameView";
    return snapshot;
}

// frame-debugger-5 campaign, PHASE3
// (PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md, Step 3.3 "Step A") -
// see FrameDebuggerData.h's own doc comment for the full contract. Pure,
// CPU-side discovery - reads graphSnapshot.passesInExecutionOrder directly,
// never the Editor's own already-built event tree/display-string labels.
std::vector<FrameDebuggerComputePassTextureWrite> CollectComputePassTextureWrites(
    const rg::RenderGraphSnapshot& graphSnapshot)
{
    std::vector<FrameDebuggerComputePassTextureWrite> result;
    for (const rg::RenderGraphPassSnapshot& pass : graphSnapshot.passesInExecutionOrder) {
        if (!pass.isComputePass || pass.isCulled) {
            continue;
        }
        for (std::size_t i = 0; i < pass.writeKinds.size() && i < pass.writeNames.size(); ++i) {
            if (pass.writeKinds[i] == rg::ResourceKind::Texture) {
                FrameDebuggerComputePassTextureWrite write;
                write.passName = pass.name;
                write.writeTextureName = pass.writeNames[i];
                result.push_back(std::move(write));
                break; // Known, accepted limitation - only the FIRST Texture-kind write per pass.
            }
        }
    }
    return result;
}

// frame-debugger-5 campaign, PHASE4
// (PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md, Step 3.1) - the
// VolumeTexture-kind sibling of CollectComputePassTextureWrites() above - see
// FrameDebuggerData.h's own doc comment for the full contract. Identical
// traversal shape, just filtering on rg::ResourceKind::VolumeTexture instead
// of rg::ResourceKind::Texture.
std::vector<FrameDebuggerComputePassVolumeTextureWrite> CollectComputePassVolumeTextureWrites(
    const rg::RenderGraphSnapshot& graphSnapshot)
{
    std::vector<FrameDebuggerComputePassVolumeTextureWrite> result;
    for (const rg::RenderGraphPassSnapshot& pass : graphSnapshot.passesInExecutionOrder) {
        if (!pass.isComputePass || pass.isCulled) {
            continue;
        }
        for (std::size_t i = 0; i < pass.writeKinds.size() && i < pass.writeNames.size(); ++i) {
            if (pass.writeKinds[i] == rg::ResourceKind::VolumeTexture) {
                FrameDebuggerComputePassVolumeTextureWrite write;
                write.passName = pass.name;
                write.writeVolumeTextureName = pass.writeNames[i];
                result.push_back(std::move(write));
                break; // Known, accepted limitation - only the FIRST VolumeTexture-kind write per pass.
            }
        }
    }
    return result;
}

// frame-debugger-4 campaign, PHASE3 - see FrameDebuggerData.h's own doc
// comment for the full contract. A plain, exhaustive if/else chain over
// already-resolved booleans - deliberately no live FrameDebuggerHistoryEntry/
// RenderTexture dependency at all.
//
// frame-debugger-5 campaign, PHASE3 (Step 3.4) - widened with the new
// `hasSelectedComputePassPreview` parameter/`ComputePassPreview` branch; every
// other branch's ORDER and OUTCOME is unchanged.
FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(bool hasEntry, bool hasPreview,
    bool hasCompositedPreview, bool isViewingGameViewLeaf, bool hasSelectedComputePassPreview)
{
    if (!hasEntry) {
        return FrameDebuggerPreviewSourceChoice::None;
    }
    if (isViewingGameViewLeaf) {
        // Explicit "GameView" leaf selection always wins - even if
        // compositedPreview/a compute-pass preview is ALSO present for this
        // captured frame (Locked Design Decision #5).
        return hasPreview ? FrameDebuggerPreviewSourceChoice::Preview : FrameDebuggerPreviewSourceChoice::None;
    }
    if (hasSelectedComputePassPreview) {
        // NEW (PHASE3) - a selected compute-dispatch leaf's own retained
        // output wins over the whole-frame compositedPreview/preview.
        return FrameDebuggerPreviewSourceChoice::ComputePassPreview;
    }
    if (hasCompositedPreview) {
        return FrameDebuggerPreviewSourceChoice::CompositedPreview;
    }
    if (hasPreview) {
        return FrameDebuggerPreviewSourceChoice::Preview;
    }
    return FrameDebuggerPreviewSourceChoice::None;
}

} // namespace gte
