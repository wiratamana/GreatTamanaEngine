#pragma once

// The ONE reviewed, thread-safe bridge a Network route handler (background
// thread - see AGENTS.md, "Networking") is allowed to touch to get real
// engine-produced data. Built as part of the network-impl-2 campaign - see
// task_manager/network-impl-2/PHASE2_CROSS_THREAD_FRAME_CAPTURE_BRIDGE.md.
//
// Deliberately Vulkan-free, Renderer-free, and engine-free: it only ever
// moves plain std::vector<std::uint8_t> PNG bytes (+ width/height ints)
// between "the network thread wants one" and "the main thread produced
// one" - every actual pixel-capturing/PNG-encoding happens elsewhere
// (Phase 3/4) and hands its *result* to this bridge, never the reverse.
//
// Owned by Application (the composition root), constructed BEFORE
// NetworkServer (see Application.h) so it can be handed into NetworkServer's
// constructor - see Phase 3. Must outlive NetworkServer.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace gte {

// Which real render target a capture request is asking for - see
// PHASE0_MASTER_STRATEGY.md's own two-endpoint design decision.
enum class FrameCaptureKind {
    Swapchain, // GET /get_swapchain - the literal, currently-presented OS window image.
    GameView,  // GET /get_game_view - the Game's own off-screen 3D-scene RenderTexture.
};

// A successfully captured, already-PNG-encoded still frame.
struct CapturedPngImage {
    std::vector<std::uint8_t> pngBytes;
    int width = 0;
    int height = 0;
};

// Why a capture request did NOT produce an image - surfaced to the network
// route as a specific HTTP status (see Phase 3/5's own route handlers).
enum class FrameCaptureFailureReason {
    TargetNotAvailable, // e.g. no Game view exists/visible this session, or the window is minimized.
    TimedOut,           // the main thread never serviced the request within the fixed timeout.
};

// See file header comment above for the full design rationale. Every method
// here is safe to call concurrently from any thread. Never hands out a raw
// pointer/reference into live engine state - every value crossing this
// boundary is a plain, independently-owned copy (CapturedPngImage's own
// std::vector, moved).
class FrameCaptureBridge {
public:
    FrameCaptureBridge() = default;
    ~FrameCaptureBridge() = default;

    FrameCaptureBridge(const FrameCaptureBridge&) = delete;
    FrameCaptureBridge& operator=(const FrameCaptureBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    // Blocks the CALLING (network) thread until either:
    //  - the main thread fulfills this request (returns the captured
    //    image), or
    //  - `timeoutMilliseconds` elapses with no fulfillment (returns
    //    FrameCaptureFailureReason::TimedOut), or
    //  - the main thread positively determines the target isn't available
    //    this frame (returns FrameCaptureFailureReason::TargetNotAvailable -
    //    see FailPendingRequest() below), or
    //  - a request of this SAME `kind` is already pending from a DIFFERENT
    //    caller (returns immediately, WITHOUT blocking at all, as a THIRD,
    //    distinct outcome - see the return type note below).
    //
    // Never blocks longer than timeoutMilliseconds. Never touches Vulkan/
    // Renderer/any engine subsystem - purely mutex/condition_variable
    // bookkeeping plus moving already-produced bytes.
    struct RequestResult {
        // Exactly one of these three is meaningful - std::nullopt on both
        // `image`/`failure` (both empty) means "another caller's request of
        // this same kind was already pending" (HTTP 503 - see Phase 3's own
        // route handler for exactly how this maps to a status code).
        std::optional<CapturedPngImage> image;
        std::optional<FrameCaptureFailureReason> failure;
        bool alreadyPending = false;
    };
    RequestResult RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    // True if a route handler is currently waiting on `kind` (i.e. the main
    // thread should actually go perform this capture this frame). A cheap,
    // side-effect-free read - never blocks.
    bool IsCaptureRequested(FrameCaptureKind kind) const;

    // Delivers a successfully captured+encoded image to whichever network
    // thread is waiting on `kind` (a safe no-op, doing nothing, if nothing
    // is currently pending for `kind` - e.g. the main thread producing a
    // capture "just in case" nobody asked for one this exact frame should
    // never happen given IsCaptureRequested()'s own guard, but this method
    // stays defensive regardless).
    void FulfillPendingRequest(FrameCaptureKind kind, CapturedPngImage image);

    // Tells whichever network thread is waiting on `kind` that this
    // request cannot be serviced right now (see
    // FrameCaptureFailureReason::TargetNotAvailable) - lets the network
    // thread fail FAST instead of waiting out the full timeout for a
    // request that can never succeed this session (e.g. /get_game_view with
    // no visible Game view).
    void FailPendingRequest(FrameCaptureKind kind, FrameCaptureFailureReason reason);

private:
    struct Slot {
        mutable std::mutex mutex;
        std::condition_variable conditionVariable;
        bool requested = false;
        bool fulfilled = false; // true once `result`/`failureReason` is meaningful.
        std::optional<CapturedPngImage> result;
        std::optional<FrameCaptureFailureReason> failureReason;
    };

    Slot& SlotFor(FrameCaptureKind kind);
    const Slot& SlotFor(FrameCaptureKind kind) const;

    Slot m_swapchainSlot;
    Slot m_gameViewSlot;
};

} // namespace gte
