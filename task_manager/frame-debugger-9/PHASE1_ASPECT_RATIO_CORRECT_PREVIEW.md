# PHASE1 — Aspect-Ratio-Correct Preview (Feature 1)

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first, especially "Locked Design
Decisions" #4 and #7, which this phase implements._

## Step 1: The Goal

The Frame Debugger's big preview image (the box under the Channels/Levels row, showing
either "Nothing drawn yet...", "No Texture", or the actual accumulated Game View at the
selected step) must display the real texture at its OWN aspect ratio — letterboxed
(black bars top/bottom) or pillarboxed (black bars left/right) as needed — never
stretched to fill the box.

## Step 2: The Situation

`src/Editor/Panels/FrameDebuggerPanel.cpp`, `BuildInspectorPane()`:

```cpp
const float previewHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);
ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
{
    if (showPreviewTexture) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_previewDescriptor)), avail);
        }
    } else {
        // ... placeholder text, already centered via ImGui::SetCursorPos() ...
    }
}
ImGui::EndChild();
```

`ImGui::Image(id, avail)` stretches the source texture to fill `avail` exactly,
regardless of its real aspect ratio. The real width/height of whatever is currently
shown is already known and available at this call site: `snapshot.renderTarget.width` /
`snapshot.renderTarget.height` (`FrameDebuggerRenderTargetInfo`, `FrameDebuggerData.h`)
— every retained preview/compositedPreview/perObjectStepPreview texture is always
created at exactly the Game View's own resolution (confirmed by reading
`FrameDebuggerHistory.cpp`'s `CaptureFrame()` and `Application/RenderPasses.cpp`'s
`AddFrameDebuggerReplayPasses()` — every one of the N replay destination textures is
sized identically to the real Game View target), so this single width/height pair is
already correct for every one of the three possible preview sources
(`Preview`/`CompositedPreview`/`PerObjectStepPreview`).

The placeholder text branch (`"No Texture"` / `"Nothing drawn yet at this point in the
frame."`) already centers itself via `ImGui::SetCursorPos()` — this phase does not need
to touch that centering logic, only the background color behind it (Locked Design
Decision #4 — solid black, applied uniformly whether an image or placeholder text is
being shown).

## Step 3: The Plan

### 3.1 — New pure helper: `FrameDebuggerData.h`/`FrameDebuggerData.cpp`

Add, right after the existing `FrameDebuggerRenderTargetInfo` struct (or any other
sensible spot near the other small POD structs in that header):

```cpp
// task_manager/frame-debugger-9 campaign, PHASE1 - a plain, ImGui-free rectangle
// (top-left corner offset + size, all relative to the AVAILABLE drawing area's own
// origin) describing where to draw a `sourceWidth` x `sourceHeight` image inside an
// `availableWidth` x `availableHeight` box so it is never stretched: scaled uniformly
// (same factor on both axes) to the LARGEST size that still fits entirely inside the
// available box, then centered - the classic "letterbox/pillarbox" fit, exactly like a
// video player or Unity's own preview boxes. Reused by BOTH the step preview
// (Panels/FrameDebuggerPanel.cpp's BuildInspectorPane()) and the frame-debugger-9
// campaign's new shader-property one-shot texture preview (PHASE3) - see
// ComputeAspectFitImageRect()'s own doc comment below for the one function that
// produces this.
struct FrameDebuggerAspectFitRect {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Pure geometry - no ImGui/Vulkan dependency at all, Tier-1-testable
// (tests/Editor/FrameDebuggerDataTests.cpp). Computes the largest FrameDebuggerAspectFitRect
// that (a) preserves `sourceWidth`/`sourceHeight`'s own aspect ratio exactly, (b) fits
// entirely within `availableWidth` x `availableHeight`, and (c) is centered within that
// available box (equal empty space split on whichever axis has slack). Degenerate-input
// guard: if `availableWidth`/`availableHeight`/`sourceWidth`/`sourceHeight` is <= 0 (a
// not-yet-laid-out ImGui frame, or a texture with a genuinely zero-sized reported
// extent), returns a rect that exactly fills the available box at (0, 0) instead of
// dividing by zero or producing a NaN/negative size - a safe, harmless fallback the
// caller can still hand straight to ImGui::Image() without a separate zero-check of its
// own.
FrameDebuggerAspectFitRect ComputeAspectFitImageRect(
    float availableWidth, float availableHeight, float sourceWidth, float sourceHeight);
```

Implementation (`FrameDebuggerData.cpp`):

```cpp
FrameDebuggerAspectFitRect ComputeAspectFitImageRect(
    float availableWidth, float availableHeight, float sourceWidth, float sourceHeight)
{
    if (availableWidth <= 0.0f || availableHeight <= 0.0f || sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
        return FrameDebuggerAspectFitRect{ 0.0f, 0.0f, std::max(0.0f, availableWidth), std::max(0.0f, availableHeight) };
    }

    const float availableAspect = availableWidth / availableHeight;
    const float sourceAspect = sourceWidth / sourceHeight;

    float fittedWidth;
    float fittedHeight;
    if (sourceAspect > availableAspect) {
        // Source is relatively WIDER than the box - width-constrained (letterbox: bars top/bottom).
        fittedWidth = availableWidth;
        fittedHeight = availableWidth / sourceAspect;
    } else {
        // Source is relatively TALLER than (or equal to) the box - height-constrained (pillarbox: bars left/right).
        fittedHeight = availableHeight;
        fittedWidth = availableHeight * sourceAspect;
    }

    FrameDebuggerAspectFitRect rect;
    rect.width = fittedWidth;
    rect.height = fittedHeight;
    rect.offsetX = (availableWidth - fittedWidth) * 0.5f;
    rect.offsetY = (availableHeight - fittedHeight) * 0.5f;
    return rect;
}
```

(`#include <algorithm>` already present or add it in `FrameDebuggerData.cpp` for
`std::max`.)

### 3.2 — Wire it into `BuildInspectorPane()` (`Panels/FrameDebuggerPanel.cpp`)

Replace the texture-preview child window body with:

```cpp
ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255)); // Locked Design Decision #4 - solid black letterbox background, always.
ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
{
    if (showPreviewTexture) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(
                avail.x, avail.y, static_cast<float>(snapshot.renderTarget.width),
                static_cast<float>(snapshot.renderTarget.height));
            const ImVec2 cursorBase = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursorBase.x + fit.offsetX, cursorBase.y + fit.offsetY));
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_previewDescriptor)),
                ImVec2(fit.width, fit.height));
        }
    } else {
        // ... existing placeholder-text branch, UNCHANGED ...
    }
}
ImGui::EndChild();
ImGui::PopStyleColor();
```

Everything else in `BuildInspectorPane()` (the RenderTarget/Channels/Levels rows above,
`BuildEventDetailsSection()` below) is untouched by this phase.

### 3.3 — Edge cases to explicitly handle / verify

- `snapshot.renderTarget.width`/`height` are both `0` on the honest empty-tree/placeholder
  snapshot (`BuildPlaceholderFrameDebuggerSnapshot()`) — but that path only matters when
  `showPreviewTexture` is `false` anyway (no descriptor exists yet), so
  `ComputeAspectFitImageRect()` is never actually called with a zero source size in
  practice; its own degenerate-guard (3.1) is still correct defensive coverage for that
  theoretical case.
- A perfectly square available box with a perfectly square texture must produce
  `offsetX == offsetY == 0` and `width`/`height` exactly filling the box (no visible
  black bar at all) — cover this as an explicit test case (3.4).
- Extremely thin/tall or thin/wide aspect ratios (e.g. a 4096×64 debug strip texture, if
  one is ever added later) must never divide by zero or produce a negative fitted size —
  covered by the pure function's own straightforward algebra (no branch can go negative
  given both inputs are `> 0` by the time that branch runs).

### 3.4 — Tests (`tests/Editor/FrameDebuggerDataTests.cpp`)

Add a new test group for `ComputeAspectFitImageRect()` covering, at minimum:
- Exact match (source aspect == available aspect) → no offset, fills exactly.
- Source wider than box (letterbox case) → `width == availableWidth`, `height <
  availableHeight`, `offsetY > 0`, `offsetX == 0`.
- Source taller than box (pillarbox case) → `height == availableHeight`, `width <
  availableWidth`, `offsetX > 0`, `offsetY == 0`.
- Degenerate zero/negative `availableWidth`/`availableHeight`/`sourceWidth`/`sourceHeight`
  input → returns the safe "fill the box at (0,0)" fallback, never NaN/negative.
- A concrete real-world case: a 417×333 texture (this campaign's own reference
  screenshot's reported resolution) inside an 800×400 box — compute the expected
  fitted rect by hand and assert it.

### 3.5 — Verification for this phase

1. Incremental compile check only (`cmake --build build` targeting whatever the
   Editor/tests targets are — no full clean rebuild).
2. Run the updated `tests/Editor/FrameDebuggerDataTests.cpp` test binary directly (or via
   `ctest -R FrameDebugger` if such a filtered target exists) to confirm the new cases
   pass — this is a narrow, fast test run, not the forbidden "full regression test".
3. OPTIONAL but encouraged (per `PHASE0_MASTER_STRATEGY.md`'s workflow rule #2): launch
   the Editor via `run_app_background`, drive the Frame Debugger via its existing HTTP
   routes (`GET /frame_debugger/open`, `/enable`, `/capture`, `/select_event?index=N`
   for a non-square LUT-producing compute leaf), and use `gte_send_request` to capture
   `GET /get_swapchain` and visually confirm the preview box now shows black bars
   instead of a stretched image. Stop the app afterward via `stop_app_background`.

### 3.6 — Deliverables

- `src/Editor/FrameDebuggerData.h` / `.cpp` — new `FrameDebuggerAspectFitRect` +
  `ComputeAspectFitImageRect()`.
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — `BuildInspectorPane()` updated to use it,
  plus the solid-black `ImGuiCol_ChildBg` push/pop.
- `tests/Editor/FrameDebuggerDataTests.cpp` — new test cases.
- `task_manager/frame-debugger-9/PHASE1_COMPLETION_REPORT.md` — written once the above
  compiles and the new tests pass; commit everything together via `git_add`/`git_commit`
  on `feature/frame-debugger-impl`.
