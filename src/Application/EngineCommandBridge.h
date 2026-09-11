#pragma once

// The ONE reviewed, thread-safe bridge a Network route handler (background
// thread) is allowed to touch to make an ECS-MUTATING request happen on the
// main thread - network-impl-3 campaign, PHASE4. Structurally mirrors
// FrameCaptureBridge.h (see that file's own header comment for the shared
// design rationale) with two deliberate differences: (1) a request here
// carries a real PAYLOAD (which shape/name/position/parent, or which name to
// delete) rather than just "which kind"; (2) per PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #5, this bridge has a SINGLE GLOBAL slot - only one
// engine command, of EITHER kind, may be in flight at a time across the
// whole bridge, never one slot per kind.
//
// Deliberately free of Game/Registry/Renderer #includes - this header only
// ever moves plain EngineCommandResults.h outcome structs (+ plain request
// data: strings/Vec3/bools) between "the network thread wants this done" and
// "the main thread did it" - the actual ECS mutation happens elsewhere
// (Game::InstantiatePrimitive()/DeleteEntityByName(), Phase 3) and hands its
// *result* to this bridge, never the reverse. Same "moves already-produced
// plain data, never a live pointer/reference" rule FrameCaptureBridge.h's own
// header comment states.
//
// Owned by Application (the composition root), constructed alongside
// m_captureBridge, BEFORE NetworkServer (so its address can be handed into
// NetworkServer's constructor) - see Application.h.

#include "../ECS/Entity.h"
#include "../Game/EngineCommandResults.h"
#include "../Math/Vec3.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

enum class EngineCommandKind {
    InstantiatePrimitive,
    DeleteEntity,
    // network-impl-5 campaign
    // (PHASE3_ENGINE_COMMAND_BRIDGE_AND_DISPATCH_EXTENSION.md) - two more
    // engine commands, sharing this SAME single-global-slot bridge (see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - unchanged
    // from network-impl-3's own original Locked Design Decision #5).
    SetEntityTrs,
    InstantiateLight,
};

// Plain request payload for one InstantiatePrimitive command - copied
// wholesale across the thread boundary (see EngineCommandRequest below).
struct InstantiatePrimitiveCommand {
    std::string shape;
    std::string requestedName;
    Vec3 worldPosition;
    bool hasParent = false;
    std::string parentName;
};

// Plain request payload for one DeleteEntity command.
struct DeleteEntityCommand {
    std::string name;
};

// One pending engine command, tagged by `kind` - EXACTLY one of
// `instantiatePrimitive`/`deleteEntity`/`setEntityTrs`/`instantiateLight` is
// meaningful, selected by `kind` (deliberately a plain tagged struct, not
// std::variant, matching this codebase's existing FrameCaptureKind +
// CapturedPngImage precedent - a single-purpose enum tag plus plain sibling
// fields, no visitor machinery needed).
struct EngineCommandRequest {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveCommand instantiatePrimitive;
    DeleteEntityCommand deleteEntity;
    // network-impl-5 campaign - reuses Game's OWN request-parameter structs
    // directly (src/Game/EngineCommandResults.h, Phase 2) rather than
    // re-declaring an identical shape as a THIRD/FOURTH "Command" struct
    // here - see this phase document's own Step 2 note on why this
    // diverges, deliberately, from the two OLDER fields immediately above.
    SetEntityTrsParams setEntityTrs;
    InstantiateLightParams instantiateLight;
};

// The completed result of one EngineCommandRequest - `kind` mirrors the
// request's own `kind` (so a caller holding only the result can still tell
// which outcome field is meaningful).
struct EngineCommandResult {
    EngineCommandKind kind = EngineCommandKind::InstantiatePrimitive;
    InstantiatePrimitiveOutcome instantiatePrimitive;
    DeleteEntityOutcome deleteEntity;
    // network-impl-5 campaign
    SetEntityTrsOutcome setEntityTrs;
    InstantiateLightOutcome instantiateLight;
};

class EngineCommandBridge {
public:
    EngineCommandBridge() = default;
    ~EngineCommandBridge() = default;

    EngineCommandBridge(const EngineCommandBridge&) = delete;
    EngineCommandBridge& operator=(const EngineCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        // Meaningful only when neither of the two flags below is set.
        std::optional<EngineCommandResult> result;
        // True (result/timedOut both meaningless) if ANOTHER command
        // (either kind) is already pending from a different caller - returns
        // IMMEDIATELY, without blocking at all, mirroring
        // FrameCaptureBridge::RequestResult::alreadyPending exactly.
        bool alreadyPending = false;
        // True (result meaningless) if `timeoutMilliseconds` elapsed with no
        // fulfillment from the main thread.
        bool timedOut = false;
    };
    SubmitResult SubmitAndWait(EngineCommandRequest request, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    // True if a route handler is currently waiting on a command - a cheap,
    // side-effect-free read, never blocks. Kept as its own method purely for
    // read-only observability (e.g. a future test/diagnostic) - Application::Run()
    // itself must NOT use this together with a separate "now fetch it" call
    // (see TryPeekPendingCommandRequest() below for why that combination is
    // unsafe).
    bool IsCommandPending() const;

    // SECOND-ITERATION FIX (this doc's own review pass): the original design
    // here was a separate IsCommandPending() check followed by a second,
    // separately-locked PeekPendingCommandRequest() call that ASSERTED
    // IsCommandPending() had just returned true. That is a genuine
    // check-then-use race: the network thread's own SubmitAndWait() can time
    // out and reset m_requested to false in the (tiny, but non-zero) window
    // between those two separate lock/unlock cycles, which would fire the
    // assert (a debug-build CRASH) or, in release, hand back a stale/garbage
    // m_request - a direct violation of this campaign's own Cross-Phase
    // Invariant #3 ("never crashes the engine"). Fixed by collapsing
    // "is one pending" and "copy it out" into ONE atomically-locked
    // operation instead - there is no gap in which the network thread can
    // observe/mutate state in between.
    //
    // Returns a COPY of the currently-pending request's data, or
    // std::nullopt if nothing is pending right now. A copy, not a reference,
    // is returned deliberately - the main thread should never hold a
    // reference into this bridge's own internal, mutex-guarded storage past
    // this one call. This is the ONLY method Application::Run() should call
    // to fetch a pending command - never resurrect the old
    // IsCommandPending() + a separate fetch-by-reference pattern.
    std::optional<EngineCommandRequest> TryPeekPendingCommandRequest() const;

    // Delivers a completed result to whichever network thread is waiting -
    // a safe no-op if nothing is currently pending (mirrors
    // FrameCaptureBridge::FulfillPendingRequest()'s own defensive no-op
    // behavior for the same reason: a request that already timed out on the
    // network side must never crash the main thread that eventually finishes
    // it anyway).
    void FulfillCommand(EngineCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false; // true once m_result is meaningful.
    EngineCommandRequest m_request;
    EngineCommandResult m_result;
};

} // namespace gte
