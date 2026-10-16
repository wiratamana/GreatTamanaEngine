#pragma once

// Engine-side hook: "a real draw just finished recording against this attachment". The Editor-side
// implementation decides whether an observer is armed for it.

#include "Extent.h"
#include "PixelFormat.h"
#include "../Renderer/RenderGraph/RecordingContext.h"

#include <cstdint>

namespace gte {

// Opaque token naming a GPU image. Only src/Renderer can open it.
enum class NativeImageToken : std::uint64_t { Null = 0 };

// The attachment one draw wrote: a color or a depth image, never both.
struct AttachmentWrite {
    NativeImageToken image = NativeImageToken::Null;
    Extent2D extent{};
    PixelFormat format = PixelFormat::Undefined;
    bool isDepth = false;
};

// Split in two on purpose: NoteCommandResult() fires while the pass's rendering instance is still open, so it
// must only remember what to copy. FlushPendingCapture() fires once per pass after that instance closed and is
// the only place that may record the copy.
class FrameDebuggerEventSink {
public:
    virtual ~FrameDebuggerEventSink() = default;

    // Bookkeeping only; must not record GPU commands.
    virtual void NoteCommandResult(const AttachmentWrite& write) = 0;

    // Records the copy for whatever NoteCommandResult() noted during the pass. No-op when nothing was noted.
    virtual void FlushPendingCapture(const rg::RecordingContext& cmd) = 0;
};

} // namespace gte
