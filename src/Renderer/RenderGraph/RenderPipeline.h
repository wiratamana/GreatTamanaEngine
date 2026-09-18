#pragma once

// render-pass-3 campaign (task_manager/render-pass-3), PHASE1
// (PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md) - the new, generic pass-
// DECLARATION layer described by GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md,
// living strictly ABOVE the existing, untouched RenderGraphBuilder::
// AddPass()/AddComputePass()/AddRenderPass() orchestrator (see
// RenderGraphBuilder.h). This file is PURE, ADDITIVE VOCABULARY with ZERO
// real consumers yet - PHASE2 is the first real wiring
// (task_manager/render-pass-3/PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md).
// Nothing under src/Application/ or src/Editor/ includes this header yet -
// see this phase's own "What We Will NOT Do".
//
// PHASE0_MASTER_STRATEGY.md's own Locked Design Decision 5 is why
// RenderPipeline::DeclareInto() below TRANSLATES its own opaque
// RenderPassId/RenderPassTagMask/RenderViewId/RenderPassEvent vocabulary
// into the EXISTING, byte-for-byte-unchanged ViewScope/RenderPassCategory/
// RenderPassDrawKind values before calling into
// RenderGraphBuilder::AddRenderPass() - that old vocabulary is never
// deleted, and this new layer is the ONLY thing that ever sees the new
// opaque types. RenderPassEvent ITSELF is the one exception to "every new
// PHASE1 type lives in this file": it lives in RenderGraphTypes.h instead,
// next to PassRecord/RenderGraphPassSnapshot, which it is also threaded
// onto - see that file's own comment on RenderPassEvent for why.
//
// namespace gte::rg, mirroring every other Render Graph file - this is
// still "the render graph module," just the declaration-layer half of it
// (see GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md's own architecture diagram:
// feature modules -> RenderPipeline -> RenderGraphBuilder::AddPass()/
// AddComputePass()).

#include "RenderGraphBuilder.h"
#include "RenderGraphTypes.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <vector>

namespace gte::rg {

// --- RenderPassId (design doc Section 8) -----------------------------------
//
// Stable, typed pass/blackboard-key identity - lookups are O(1) integer
// compares; `debugName` (RenderPassDesc, below) remains purely for
// humans/UI, fully decoupled from identity/lookup.

struct RenderPassId {
    std::uint64_t hash = 0;

    constexpr bool operator==(const RenderPassId&) const noexcept = default;
};

// A small, deterministic FNV-1a-style hash over the literal's own bytes,
// evaluated ENTIRELY at compile time (consteval - never costs anything at
// runtime, matching this design's own "a tag test is a free bitwise AND"
// performance discipline extended to identity hashing too). Reuses the
// exact same offset-basis/prime constants this codebase's existing
// HashJobName() (src/Editor/JobsPanelData.cpp) already uses for its own
// deterministic, stable-across-runs string hash - widened to a 64-bit
// accumulator purely so RenderPassId's own 64-bit `hash` field is filled
// directly, with no extra truncation/widening step. A deliberate reuse of
// an already-proven constant pair rather than inventing a second,
// differently-parameterized FNV-1a variant for no reason.
consteval RenderPassId operator""_passId(const char* s, std::size_t n) noexcept
{
    std::uint64_t hash = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i]));
        hash *= 16777619u;
    }
    return RenderPassId{ hash };
}

#ifndef NDEBUG
// Debug-only "which source string did this hash come from" lookup (design
// doc Section 8) - purely for assertion messages/future tooling, never
// used for runtime identity comparisons (identity is always the 64-bit
// hash itself).
//
// IMPORTANT: operator""_passId above is deliberately consteval, so it can
// NEVER itself call into either function below - a consteval function is
// evaluated ENTIRELY at compile time and cannot leave any runtime-visible
// side effect behind (the standard forbids a mutable function-local static
// inside a constexpr/consteval function for exactly this reason - there is
// no way for "the running program" to observe anything a consteval
// evaluation did). RegisterPassIdDebugName()/DebugNameForPassId() are
// therefore a genuinely SEPARATE, ordinary (non-consteval) pair of runtime
// functions a future call site can invoke explicitly wherever it mints a
// RenderPassId it wants to be debuggable (e.g. a future RenderPipeline::
// Register() call, once a real consumer exists) - nothing calls either one
// yet in this phase (see this phase's own Definition of Done: "zero real
// consumers"), so this gap has no observable effect today. See this
// phase's own completion report for the full write-up of this deliberate
// deviation from the phase doc's own literal wording.
void RegisterPassIdDebugName(RenderPassId id, const char* name) noexcept;
const char* DebugNameForPassId(RenderPassId id) noexcept; // "<unknown>" if never registered.
#endif

// --- RenderPassTag / RenderPassTagMask (design doc Section 7) --------------
//
// Fixed 64-bit bitmask (design doc Section 0, point 4) - a tag test stays a
// free bitwise AND. No feature-specific tag VALUES live here - per the
// design doc's own Section 7, those belong in each feature's own header
// (e.g. a future AtmosphereTags::Lut), never in this shared core file.

struct RenderPassTag {
    std::uint64_t bit = 0;
};
using RenderPassTagMask = std::uint64_t;

// --- RenderViewId (design doc Section 7) -----------------------------------
//
// Opaque view identifier - Shared() is the only built-in value this layer
// defines; Named() lets any future named view (beyond today's Game/Scene)
// exist for free, since view identity is a hashed opaque name rather than a
// fixed enum from the start (design doc Section 0, point 6).

class RenderViewId {
public:
    RenderViewId() noexcept = default; // Defaults to the same reserved hash Shared() returns.

    static RenderViewId Shared() noexcept { return RenderViewId{}; }

    // Hashed, opaque - the same FNV-1a algorithm/constants as
    // operator""_passId above, just evaluated at ordinary RUNTIME (this
    // takes a plain `const char*`, not a literal operand, so it cannot be
    // consteval).
    static RenderViewId Named(const char* name) noexcept
    {
        std::uint64_t hash = 2166136261u;
        if (name != nullptr) {
            for (const char* p = name; *p != '\0'; ++p) {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(*p));
                hash *= 16777619u;
            }
        }
        RenderViewId view;
        // Reserve 0 exclusively for Shared() - see this class's own header
        // comment. A real name hashing to exactly 0 is astronomically
        // unlikely, but nudged to 1 defensively rather than left as a
        // silent collision with Shared().
        view.m_hash = (hash != 0) ? hash : 1;
        return view;
    }

    bool operator==(const RenderViewId&) const noexcept = default;

private:
    std::uint64_t m_hash = 0;
};

// --- RenderPassDesc (design doc Section 2 - WITH a deliberate, documented
// extension beyond the design doc's own shown shape) -----------------------
//
// The core has no enumerated knowledge of what a pass "is" - a plain value
// type, produced by any feature, consumed generically by RenderPipeline
// below with no branching on "what kind of pass is this".

struct RenderPassDesc {
    RenderPassId id;
    const char* debugName = nullptr;
    PassKind kind = PassKind::Graphics;
    RenderPassEvent order = RenderPassEvent::Opaques;
    RenderPassTagMask tags = 0;
    RenderViewId view = RenderViewId::Shared();

    // render-pass-3 campaign, PHASE1 - deliberate, documented EXTENSION
    // beyond GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md's own Section 2 shape.
    // The design doc's own Decision 1 assumed the OLD ViewScope/
    // RenderPassCategory/RenderPassDrawKind fields would eventually be
    // deleted once every pass migrated - PHASE0_MASTER_STRATEGY.md's own
    // Locked Design Decisions 2/4/5 explicitly keep them alive FOREVER
    // instead (the Frame Debugger and its own Editor-only debug passes
    // still read them directly). A pass declared through THIS new layer
    // must therefore still carry a real, correct legacyCategory/drawKind
    // so RenderPipeline::DeclareInto() (below) can stamp them onto the
    // underlying PassRecord exactly as if the old AddRenderPass() overload
    // had been called directly - the Frame Debugger must never be able to
    // tell the difference between a pass declared the old way and one
    // declared through this new layer. `view` (above) is what
    // DeclareInto() translates into the legacy `ViewScope` - see PHASE3's
    // own translation table (this phase uses a trivial, Shared-only
    // stand-in - see DeclareInto()'s own comment below).
    RenderPassCategory legacyCategory = RenderPassCategory::General;
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;

    std::function<void(RenderGraphBuilder::PassBuilder&)> setup;
    std::function<void(PassContext&)> execute;
};

// --- RenderPassBlackboard (design doc Section 4) ---------------------------
//
// Cross-provider data hand-off: a generic, opaque-keyed slot store, not a
// hardcoded named field. Any provider can Publish()/Fetch() any handle type
// under any caller-chosen RenderPassId key; the mechanism is generic, only
// the keys a given feature happens to use are feature-specific, and those
// keys live in that feature's own header, never in core.

class RenderPassBlackboard {
public:
    // OVERWRITES an existing slot with the same key if one already exists
    // this frame (last-publish-wins) rather than pushing a duplicate - a
    // linear scan for a matching key before appending is correct and cheap
    // at the realistic single-digit-to-low-tens key count design doc
    // Section 11 already commits to.
    template <typename T>
    void Publish(RenderPassId key, T value)
    {
        for (Slot& slot : m_slots) {
            if (slot.key == key) {
                slot.value = std::any(std::move(value));
#ifndef NDEBUG
                slot.wasFetched = false; // A fresh publish this frame - not yet fetched again.
#endif
                return;
            }
        }

        Slot slot;
        slot.key = key;
        slot.value = std::any(std::move(value));
        m_slots.push_back(std::move(slot));
    }

    // std::nullopt for a never-published key, OR for a key published under
    // a DIFFERENT type than `T` (relies on std::any_cast's own pointer-
    // overload type-mismatch behavior - returns nullptr rather than
    // throwing - see RenderPipelineTests.cpp for an explicit test of this,
    // never assumed).
    template <typename T>
    std::optional<T> Fetch(RenderPassId key) const
    {
        for (const Slot& slot : m_slots) {
            if (slot.key == key) {
#ifndef NDEBUG
                slot.wasFetched = true;
#endif
                if (const T* value = std::any_cast<T>(&slot.value)) {
                    return *value;
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Called once at the start of each frame's declaration. Clears entries
    // but keeps whatever backing storage was already reserved from the
    // previous frame's high-water mark (vector::clear() never releases
    // capacity) - avoids rebuilding the container from nothing every
    // single frame (design doc Section 11's "reused, not reallocated"
    // performance discipline).
    void BeginFrame() { m_slots.clear(); }

    // Testing-only accessor (mirrors this codebase's own established
    // "...ForTesting()" naming convention - see e.g.
    // FrameProfiler::ResetForTesting(), src/Profiling/FrameProfiler.h) -
    // lets RenderPipelineTests.cpp directly confirm the "reused, not
    // reallocated" BeginFrame() contract (design doc Section 4/11) without
    // exposing `m_slots` itself. Always compiled (not debug-gated) since it
    // has no behavioral effect of its own - a plain, side-effect-free
    // capacity query.
    std::size_t SlotCapacityForTesting() const noexcept { return m_slots.capacity(); }

#ifndef NDEBUG
    // Debug builds only: after every provider has run, any key that was
    // Publish()'d but never Fetch()'d this frame is reported once, so a
    // dangling hand-off is visible instead of silently doing nothing.
    // Compiles to nothing in release. Implemented as a soft, non-fatal log
    // line (this codebase's existing std::fprintf(stderr, ...) logging
    // convention - see Application.cpp/NetworkServer.cpp/
    // VulkanInstance.cpp for the established precedent), NOT a hard
    // assert()/crash - see this phase's own "What We Will NOT Do" for why
    // a usability nuisance is an acceptable default here, while a hard
    // crash the first time a legitimately-unused-this-frame publish
    // happens (e.g. GPU Skinning publishes but nothing is animating and
    // Opaque never actually fetches this particular frame) is not.
    void ReportUnusedPublishesIfAny() const
    {
        for (const Slot& slot : m_slots) {
            if (!slot.wasFetched) {
                std::fprintf(stderr,
                    "RenderPassBlackboard: a value was published under key \"%s\" but never fetched this "
                    "frame.\n",
                    DebugNameForPassId(slot.key));
            }
        }
    }
#endif

private:
    struct Slot {
        RenderPassId key;
        std::any value;
#ifndef NDEBUG
        mutable bool wasFetched = false;
#endif
    };

    // Flat, linearly scanned rather than a hash map - see design doc
    // Section 4/11: the realistic number of live keys per frame is small
    // (single digits to low tens), where a flat vector is both faster and
    // lighter than a hashed container.
    std::vector<Slot> m_slots;
};

// --- RenderPassFrameContext (design doc Section 5 - WITH the additional
// `finalTextureOutputs`/`finalVolumeTextureOutputs` fields this codebase's
// real integration needs) --------------------------------------------------

struct RenderPassFrameContext {
    std::vector<RenderViewId> activeViews;
    RenderViewId currentView = RenderViewId::Shared(); // Stamped by RenderPipeline before each PerActiveView provider call.
    RenderPassBlackboard& blackboard;

    // render-pass-3 campaign, PHASE1 - a real, concrete resolution of a gap
    // GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md leaves as a "Phase C
    // implementation detail": SOME final TextureHandle/VolumeTextureHandle
    // values genuinely need to be added to RenderGraph::Execute()'s own
    // `finalOutputs`/`KeepVolumeTextureOutput()` root set, or
    // RenderGraphCompiler::Compile()'s backward-reachability culling scan
    // silently removes their writing pass. Rather than inventing a second,
    // parallel produce/consume declaration system (explicitly rejected by
    // the design doc's own Decision 2), any PROVIDER that owns a handle
    // needing this treatment simply appends it here directly - the caller
    // (Application::Run(), PHASE3) reads both vectors back out AFTER
    // DeclareInto() returns and forwards them to the exact same
    // `outputs`/`KeepVolumeTextureOutput()` mechanism it already uses today.
    std::vector<TextureHandle> finalTextureOutputs;
    std::vector<VolumeTextureHandle> finalVolumeTextureOutputs;

    // ... any other plain, opaque-to-the-pipeline per-frame data
    // (camera/target/dt) is added here by whichever LATER phase first
    // needs it (PHASE2/PHASE3) - this phase does not need to guess every
    // field up front; adding a field here later is a trivial, additive,
    // zero-risk change (mirrors PassRecord's own "append at the end"
    // convention).
};

// --- RenderPassProvider / ProviderScope (design doc Section 3) ------------

using RenderPassProvider =
    std::function<void(const RenderPassFrameContext& frame, std::vector<RenderPassDesc>& outPasses)>;

// Once: invoked exactly one time per frame-declaration call.
// PerActiveView: invoked once per entry in frame.activeViews, with the
// current view already stamped into that specific invocation's frame context.
enum class ProviderScope { Once, PerActiveView };

// --- RenderPipeline ---------------------------------------------------------
//
// Owns a list of registered providers, collects RenderPassDesc values from
// them once per frame-declaration call, sorts by RenderPassEvent, and feeds
// each one into the render graph - the whole point of this new layer (see
// this file's own header comment).

class RenderPipeline {
public:
    void Register(const char* debugName, ProviderScope scope, RenderPassProvider provider)
    {
        m_providers.push_back(Entry{ debugName, scope, std::move(provider) });
    }

    // Light escape hatch (design doc Section 0, point 5) - registering
    // everything once at startup is sufficient; a full unregister-at-
    // runtime facility is not a priority. Linear scan by debugName
    // pointer-OR-content match (mirrors this codebase's own
    // RenderGraphNameSlotTable "reuse by identical pointer AND by equal-
    // content-different-pointer" convention) - not load-bearing, nothing
    // calls this yet.
    void Unregister(const char* debugName)
    {
        if (debugName == nullptr) {
            return;
        }

        for (auto it = m_providers.begin(); it != m_providers.end();) {
            const bool matches = it->debugName == debugName
                || (it->debugName != nullptr && std::strcmp(it->debugName, debugName) == 0);
            if (matches) {
                it = m_providers.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Called once per graph-build callback. Loops active views for
    // PerActiveView providers, asks every provider "what do you want to
    // contribute", sorts the combined result by `order`, then feeds each
    // one into RenderGraphBuilder - the only place that still calls
    // AddPass()/AddComputePass()/AddRenderPass().
    void DeclareInto(RenderGraphBuilder& builder, RenderPassFrameContext& frame)
    {
        m_scratchCollected.clear(); // Reused, not reconstructed - see this class's own field comment below.

        for (const Entry& entry : m_providers) {
            if (entry.scope == ProviderScope::Once) {
                entry.provider(frame, m_scratchCollected);
            } else { // PerActiveView
                for (const RenderViewId& view : frame.activeViews) {
                    frame.currentView = view;
                    entry.provider(frame, m_scratchCollected);
                }
            }
        }

        std::stable_sort(m_scratchCollected.begin(), m_scratchCollected.end(),
            [](const RenderPassDesc& a, const RenderPassDesc& b) { return a.order < b.order; });

        for (RenderPassDesc& desc : m_scratchCollected) {
            // PHASE0_MASTER_STRATEGY.md's own Locked Design Decision 5 -
            // translate into the OLD, byte-for-byte-unchanged
            // AddRenderPass() call. This phase has no real per-view
            // translation table yet (that is PHASE3's job, migrating
            // Game/Scene View onto this system) - every declared pass is
            // translated as ViewScope::Shared here, regardless of
            // `desc.view`, exactly as this phase's own doc (Step 3.1)
            // specifies: "this phase's own test double can use a trivial
            // Shared-only translation, since no real per-view translation
            // table exists to call into yet."
            constexpr ViewScope translatedViewScope = ViewScope::Shared;
            builder.AddRenderPass(desc.debugName, desc.kind, translatedViewScope, desc.legacyCategory, desc.setup,
                desc.execute, desc.drawKind, desc.order);
        }
    }

private:
    struct Entry {
        const char* debugName;
        ProviderScope scope;
        RenderPassProvider provider;
    };

    std::vector<Entry> m_providers;

    // Owned once, reused every frame: cleared (not reconstructed) at the
    // start of each DeclareInto() call so its capacity survives across
    // frames instead of reallocating from empty every time (design doc
    // Section 11).
    std::vector<RenderPassDesc> m_scratchCollected;
};

} // namespace gte::rg
