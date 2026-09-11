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
#include <string>
#include <vector>

namespace gte {

// Which real render target a capture request is asking for - see
// PHASE0_MASTER_STRATEGY.md's own two-endpoint design decision.
enum class FrameCaptureKind {
    Swapchain, // GET /get_swapchain - the literal, currently-presented OS window image.
    GameView,  // GET /get_game_view - the Game's own off-screen 3D-scene RenderTexture.
    // network-impl-4 campaign, Phase 4 - GET /get_texture. Unlike the two
    // above, a request of THIS kind carries a dynamic payload (which
    // texture, which channel) - see RequestedTextureName()/
    // RequestedTextureChannel() below.
    NamedTexture,
};

// network-impl-4 campaign, Phase 4 - which half of a named texture a
// GET /get_texture request wants. Color is the default (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 5).
enum class DebugTextureChannel {
    Color,
    Depth,
};

// A successfully captured, already-PNG-encoded still frame.
struct CapturedPngImage {
    std::vector<std::uint8_t> pngBytes;
    int width = 0;
    int height = 0;
    // network-impl-4 campaign, Phase 4 - PHASE0_MASTER_STRATEGY.md's Locked
    // Design Decision 4. Meaningful ONLY for FrameCaptureKind::NamedTexture
    // (left at its default, 0, for Swapchain/GameView, which have no
    // equivalent "how many frames old" concept and whose own route handler
    // never reads this field at all - see Phase 5). Computed by
    // Application::Run() at the exact moment it services the request, as
    // `RenderGraph::CurrentDebugTextureFrameCounter() -
    // snapshot.lastUpdatedFrameCounter` (see Application.cpp).
    std::uint64_t framesSinceUpdate = 0;
};

// network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
// one fully pre-shaped row of GET /list_textures' response, published once
// per real engine frame by Application::Run() (see PublishTextureList()
// below). Deliberately a SEPARATE, nearly-identical type from
// gte::Network::TextureListEntryView (src/Network/NetworkRoutes.h), not a
// shared one - this keeps BOTH of this engine's "a struct must never cross
// this exact layer boundary" rules intact at once:
//   - THIS class must stay Vulkan/Renderer/RenderGraph-free (see this
//     file's own header comment, above) - so every field here is an
//     ALREADY-RESOLVED plain scalar, never an
//     rg::DebugTextureSnapshot/rg::ExecuteTimingMode. Application::Run()
//     (which DOES know about RenderGraph) resolves `regime`/`format` to
//     plain strings and `framesSinceUpdate` to a plain integer BEFORE ever
//     calling PublishTextureList() below - see Step 3.4.
//   - NetworkRoutes.h's own response-builder functions must never take a
//     struct OWNED BY A DIFFERENT LAYER as a parameter (see
//     BuildInstantiatePrimitiveResponseJson()'s own doc comment) - so this
//     struct never crosses into NetworkRoutes.h either. NetworkServer.cpp
//     (which already depends on BOTH this header and NetworkRoutes.h) is
//     the ONE place that copies this struct's fields into a fresh
//     gte::Network::TextureListEntryView, one field at a time, right before
//     calling BuildListTexturesResponseJson() - see Step 3.6.
// A few bytes of per-request copying for a single-digit-to-low-double-
// digit-sized list is negligible - see PHASE0_MASTER_STRATEGY.md's own
// Locked Design Decision 8 for this engine's general tolerance for this
// class of cost.
struct PublishedTextureListEntry {
    std::string name;
    std::string regime; // "synchronous" or "pipelined" - see Application.cpp's own ToDebugTextureRegimeString() helper (Step 3.4).
    std::string format;  // e.g. "B8G8R8A8_UNORM" - the texture's COLOR VkFormat, already stringified - see Application.cpp's own DebugTextureColorFormatName() helper (Step 3.4). Never the depth format; see hasDepth below for whether one even exists.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasDepth = false;
    std::uint64_t framesSinceUpdate = 0;

    // network-impl-6 campaign, Phase 5. "texture2d" (the only kind that
    // existed before this campaign) or "texture3d". Every OLD call site
    // building a 2D entry is updated to set this explicitly to "texture2d"
    // (never left to an implicit/defaulted value, so it's obvious at each
    // call site which kind is being built) - see Application.cpp.
    std::string kind = "texture2d";

    // Meaningful ONLY when kind == "texture3d" (the volume's Z/depth texel
    // count) - always 0 for a "texture2d" entry. NOT to be confused with
    // hasDepth above (a 2D texture's OWN depth-BUFFER availability) - a
    // volume entry always has hasDepth == false (see VolumeTarget.h: a
    // volume texture has no depth-companion concept at all), that field
    // is untouched/reused as-is for this new kind, just always false.
    std::uint32_t depth = 0;
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
    // `textureName`/`channel` are meaningful ONLY when
    // `kind == FrameCaptureKind::NamedTexture` - ignored for the other two
    // kinds, exactly like an unused parameter.
    RequestResult RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds = 3000,
        const std::string& textureName = "", DebugTextureChannel channel = DebugTextureChannel::Color);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    // True if a route handler is currently waiting on `kind` (i.e. the main
    // thread should actually go perform this capture this frame). A cheap,
    // side-effect-free read - never blocks.
    bool IsCaptureRequested(FrameCaptureKind kind) const;

    // Valid ONLY when IsCaptureRequested(FrameCaptureKind::NamedTexture) is
    // currently true - the exact (textureName, channel) the currently-pending
    // request asked for. Returns a default-constructed
    // {"", DebugTextureChannel::Color} if nothing is currently pending for
    // this kind - never garbage.
    //
    // NOTE: this is intentionally TWO separate accessors (plus
    // IsCaptureRequested() as a third, prior call) rather than one combined
    // atomic read. A concurrent request-of-a-different-name racing in between
    // these calls is possible in principle but harmless in practice: whichever
    // request is actually pending by the time this phase's own Application.cpp
    // block reaches FulfillPendingRequest()/FailPendingRequest() is always the
    // SAME request whose name/channel were just read here - a completion
    // always targets "whatever this slot's current request is", never a
    // stale one, since RequestCaptureAndWait() only marks a request complete
    // for the specific slot state a caller is actually waiting on. No
    // redesign is needed here purely to remove this theoretical interleaving.
    std::string RequestedTextureName() const;
    DebugTextureChannel RequestedTextureChannel() const;

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

    // --- GET /list_textures support (network-impl-4 campaign, Phase 5) ------
    // Unlike RequestCaptureAndWait() above, there is nothing to "wait for" here
    // - the list is whatever it is right now. Guarded by its own small,
    // dedicated mutex (m_textureListMutex) - deliberately NO condition
    // variable, since nothing ever blocks on this.

    // --- Called from the MAIN thread (Application::Run()) only --------------
    // Publishes a fresh, COMPLETE snapshot of every currently-known texture -
    // OVERWRITES whatever was published before wholesale (never merges/
    // appends), so an empty vector correctly clears a previously non-empty
    // list rather than leaving stale entries behind. Called once per real
    // engine frame - see Application.cpp's own wiring, Step 3.4.
    void PublishTextureList(std::vector<PublishedTextureListEntry> entries);

    // --- Called from the NETWORK thread (a route handler) only --------------
    // A cheap, thread-safe COPY of whatever was last published - never blocks.
    // Returns an empty vector if PublishTextureList() has never been called yet
    // this session (e.g. queried before the very first Run() iteration
    // completes) - a valid, normal state, never an error.
    std::vector<PublishedTextureListEntry> GetPublishedTextureList() const;

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
    Slot m_namedTextureSlot;
    // Guarded by m_namedTextureSlot.mutex - see .cpp for the exact locking
    // discipline (set once, by RequestCaptureAndWait(), right before it marks
    // `requested = true`; read by RequestedTextureName()/
    // RequestedTextureChannel() under the same lock).
    std::string m_requestedTextureName;
    DebugTextureChannel m_requestedTextureChannel = DebugTextureChannel::Color;

    // network-impl-4 campaign, Phase 5 - GET /list_textures support. Guarded
    // by its own dedicated mutex, independent of every Slot above.
    mutable std::mutex m_textureListMutex;
    std::vector<PublishedTextureListEntry> m_publishedTextureList;
};

} // namespace gte
