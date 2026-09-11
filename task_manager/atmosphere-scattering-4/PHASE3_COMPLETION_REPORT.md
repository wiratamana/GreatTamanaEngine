# PHASE3 COMPLETION REPORT — Permanent Regression Diagnostic: "Validate Aerial Perspective Sky Purity"

**Parent:** `PHASE0_MASTER_STRATEGY.md`
**Phase document:** `PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md`
**Depends on:** `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md` (this phase reuses
its `ShouldBypassAerialPerspectiveComposite()` oracle directly) and
`PHASE2_SHADER_PASSTHROUGH_FIX.md` (the fix this phase's tool is meant to
catch a future regression of) — both re-read/confirmed committed and clean
before starting.
**Status:** Complete. Compile check green, engine launched and the new
button confirmed **PASS** with the Phase 2 fix in place, and (optional,
recommended step) confirmed **FAIL** when the fix was temporarily reverted,
then the fix was re-applied and re-confirmed **PASS** before committing.

---

## 1. What was done

### 1.1 New files

- **`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h`** — created
  essentially verbatim from the phase document's own Step 3.1 sketch (which
  had already been corrected in-place for the `const rg::RenderGraph&`
  signature and the "no separate `ImGuiEditorLayer.h`" fact). Declares
  `AtmosphereAerialPerspectiveSkyPurityResult` (`succeeded`/`failureReason`/
  `width`/`height`/`skyPixelCount`/`mismatchingSkyPixelCount`/
  `maxSkyPixelChannelDelta`/`meanSkyPixelChannelDelta`/`toleranceUnorm8`/
  `Passed()`), `ToDiagnosticString()`, and
  `ValidateAerialPerspectiveSkyPurity(Renderer&, const rg::RenderGraph&, const char* preCompositeColorTextureName, const char* compositedColorTextureName, double toleranceUnorm8 = 1.0/255.0)`.
- **`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp`** —
  implements the two functions above, following the document's Step 3.2
  sketch **with one genuine, load-bearing correction found during this
  session's own runtime verification** — see §2 "Deviations" below for the
  full story (a real, reproducible false-FAIL bug in the document's own
  sketch, not a cosmetic drift).

### 1.2 `CMakeLists.txt`

Re-read fresh immediately before editing. The existing
`src/Editor/AtmosphereAerialPerspectiveLutInspection.h`/`.cpp` lines were at
0-based indices 581/582 (drifted slightly from the phase document's own
stale "around line 579-580" reference, as expected). Inserted the two new
lines immediately after them:

```
src/Editor/AtmosphereAerialPerspectiveLutInspection.h        (unchanged, line 581)
src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp      (unchanged, line 582)
src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h  (new, line 583)
src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp (new, line 584)
```

### 1.3 `src/Editor/Panels/AtmospherePanel.h`/`.cpp`

Re-read fresh immediately before editing; matched the phase document's own
snapshot exactly (no drift from PHASE1/PHASE2, which never touched this
file). Extended `BuildAtmospherePanel(...)`'s signature with two new
TRAILING parameters, appended after the existing four:

```cpp
void BuildAtmospherePanel(EditorContext& ctx, AtmosphereSettings& settings, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer,
    std::optional<AtmosphereTransmittanceLutValidationResult>& lastValidationResult,
    std::optional<AtmosphereAerialPerspectiveLutInspectionResult>& lastAerialInspectionResult,
    const rg::RenderGraph& renderGraph,
    std::optional<AtmosphereAerialPerspectiveSkyPurityResult>& lastSkyPurityResult);
```

with a matching `namespace rg { class RenderGraph; }` forward declaration
added right next to the existing `class AtmosphereLutRenderer;` forward
declaration, mirroring `RenderGraphPanel.h`'s own confirmed idiom.
`AtmospherePanel.cpp` gained a new "Validate Aerial Perspective Sky Purity"
button + PASS/FAIL/ERROR readout, added right after the existing "Inspect
Aerial Perspective LUT" block, exactly as specified — a
`ImGui::TextColored`/`ImGui::TextWrapped` pair driven by `r.Passed()` (green
PASS, red FAIL, red ERROR when `!r.succeeded`).

### 1.4 `src/Editor/ImGuiEditorLayer.cpp`

Confirmed (as the phase document's own "Verified against real source on
2026-09-11" note already stated, and re-confirmed again fresh here) there is
NO separate `ImGuiEditorLayer.h` — the whole class lives inline in this one
`.cpp`. Re-read fresh immediately before editing:

- Added `#include "AtmosphereAerialPerspectiveSkyPurityValidation.h"` next to
  the existing `AtmosphereAerialPerspectiveLutInspection.h`/
  `AtmosphereTransmittanceLutValidation.h` includes (now lines 1-3, shifted
  by +2 from the document's own pre-edit snapshot, as expected since two new
  include lines were inserted).
- Added `std::optional<AtmosphereAerialPerspectiveSkyPurityResult>
  m_lastAerialPerspectiveSkyPurityResult;` right after the existing
  `m_lastAerialPerspectiveLutInspection` member (confirmed at line 812 at
  edit time — drifted by +2 from the document's stale "810" reference,
  exactly the expected shift from the two new include lines added above it
  in the same file).
- Updated the `BuildAtmospherePanel(...)` call site (confirmed at line 549
  at edit time — drifted by +2 from the document's stale "548" reference,
  same cause) to pass `renderGraph` (already in scope in `BuildUI(Game&,
  Renderer&, const rg::RenderGraph& renderGraph, ...)`) and the new member.

## 2. Deviations from the phase document — a genuine bug found and fixed

The phase document's own Step 3.2 sketch compares `preColorRaw`/
`postColorRaw`'s raw bytes **channel-index-for-channel-index** (`preTexel[0]`
vs. `postTexel[0]`, etc.) with no regard for each texture's own physical
pixel format. This turned out to be a **real, reproducible bug** in the
sketch itself, caught only by actually running the tool (not by static
re-reading):

- `"GameView"` (the pre-composite target) is created via
  `Renderer::CreateRenderTexture(..., VK_FORMAT_UNDEFINED, ...)`, which
  resolves to `Renderer::ColorFormat()` — this engine's swapchain-negotiated
  format, confirmed to be `VK_FORMAT_B8G8R8A8_UNORM` (BGRA byte order,
  `VulkanSwapchain.cpp`'s `ChooseSurfaceFormat()`).
- `"GameViewComposited"` (the post-composite target) is created via
  `AtmosphereLutRenderer::EnsureAerialPerspectiveCompositeViewInitialized()`
  with an **explicit, hardcoded** `VK_FORMAT_R8G8B8A8_UNORM` (RGBA byte
  order) — required because a compute shader storage image
  (`layout(binding = 3, rgba8) uniform writeonly image2D`) needs a format
  Vulkan guarantees `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` for, which BGRA
  formats are not guaranteed to support.

Comparing raw bytes without accounting for this produced a real,
reproducible **false FAIL** on the very first run: `828990 sky pixels
checked, 415413 mismatching, max channel delta 0.380392, mean channel delta
0.148685` — a large, smooth, brightness-dependent (not random) delta,
exactly the signature of an R/B channel swap on a blue-sky gradient, not a
genuine composite-pass bug. This is precisely the same "caller decides
whether a BGRA→RGBA swizzle is needed based on the real captured format"
rule `src/Encoding/PixelConversion.h`'s own `ConvertBgraToRgbaInPlace()`
already documents for this exact same engine quirk (used elsewhere for
`GET /get_swapchain`/`GET /get_game_view`) — the phase document's own sketch
simply didn't apply it here.

**Fix applied** (in the shipped `.cpp`, not a deviation from the document's
*intent*, only from its literal byte-comparison sketch): two small local
helpers, `IsBgraColorFormat(VkFormat)` and
`ReadLogicalRgb(const std::uint8_t* pixel, VkFormat format, std::uint8_t outRgb[3])`,
independently resolve each texture's own texel into its LOGICAL (R, G, B)
triplet before comparing, using each snapshot's own real
`target.format` (`preSnapshot->target.format`/`postSnapshot->target.format`)
rather than assuming both textures share one channel order. After this fix,
the exact same scene reported **PASS, 0/828990 mismatching, max delta
0.000000, mean delta 0.000000** — confirming the fix was correct, not a
loosened tolerance papering over a real bug (the tolerance itself,
`1/255 ≈ 0.003922`, was never touched).

No other deviation: every other field name/function signature/line-number
citation in the phase document (`RenderTarget`, `Renderer::CapturedRawPixels`/
`CaptureImagePixels()`, `DebugTextureSnapshot`, `AtmospherePanel`'s parameter
list, `RenderGraphPanel.h`'s forward-declaration idiom, `DepthVisualization`'s
three depth formats) matched the real source exactly as written, confirming
that document's own "Verified against real source on 2026-09-11" note.

## 3. Compile check

```
cmake --build build --target GreatTamanaEngine
```

from the repository root — **success**, zero compile errors, both before and
after the BGRA/RGBA fix above (`GreatTamanaEngine.exe` relinked cleanly each
time; the KTX-Software "cannot describe anything"/git-version warning in the
build log is pre-existing, unrelated third-party-fetch noise).

## 4. Runtime verification

Launched the Editor (`run_app_background`), maximized the window, and drove
the UI via simulated mouse clicks/scroll (PowerShell + `user32.dll`
`SetCursorPos`/`mouse_event`/`ClientToScreen`, screenshotted via
`GET /get_swapchain` after each interaction — no GUI-input-automation tool
existed for this native SDL/Vulkan window before this session; the small
helper scripts used were deleted again afterward, they are not part of this
phase's deliverable).

1. **Button exists**: clicking the "Atmosphere" tab (bottom dock) and
   scrolling down shows a third button, **"Validate Aerial Perspective Sky
   Purity"**, right after "Inspect Aerial Perspective LUT" — matches the
   phase document's placement exactly.
2. **PASS with the Phase 2 fix in place** (default scene: `Entity 0
   (Camera)` only, no meshes — 100% of the "Game" view is sky):
   ```
   PASS
   Aerial Perspective Sky Purity Validation: 1359x610, 828990 sky pixel(s) checked
     mismatching sky pixels: 0 / 828990
     max channel delta:      0.000000
     mean channel delta:     0.000000
     tolerance:              0.003922
   ```
   `N = 828990 > 0`, as required.
3. **Optional, recommended confidence check — fix temporarily reverted**:
   `src/Shaders/AtmosphereAerialPerspectiveComposite.comp`'s new bypass
   condition was changed from `if (rawDepth >= 0.999999)` to `if (false)`
   (disabling the pass-through branch entirely, so every pixel — including
   sky — falls through to the full aerial-volume sample/blend), rebuilt, and
   re-tested:
   ```
   FAIL
   Aerial Perspective Sky Purity Validation: 1359x610, 828990 sky pixel(s) checked
     mismatching sky pixels: 414495 / 828990
     max channel delta:      0.235294
     mean channel delta:     0.096944
     tolerance:              0.003922
   ```
   Confirms the tool genuinely detects this exact regression class. The
   shader was then reverted back to `if (rawDepth >= 0.999999)` (confirmed
   byte-for-byte identical to the pre-revert file via a fresh `read_file`),
   rebuilt, and re-tested one final time — **PASS, 0/828990 mismatching**,
   confirming the restoration was clean. `git_status` at the end of this
   phase shows `AtmosphereAerialPerspectiveComposite.comp` as **unmodified**
   (not in the changed-files list) — the temporary revert was never
   committed.
4. `stop_app_background` after each run.

## 5. Final file line counts (for the record)

- `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h` — 90 lines.
- `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp` — 210 lines
  (larger than the phase document's own ~156-line sketch, due to the
  BGRA/RGBA fix's two new helper functions plus their doc comments — see §2).
- `CMakeLists.txt` — 2 new lines inserted at (now) 583-584.
- `src/Editor/Panels/AtmospherePanel.h` — 77 lines (was 56).
- `src/Editor/Panels/AtmospherePanel.cpp` — 113 lines (was 96).
- `src/Editor/ImGuiEditorLayer.cpp` — net +9 lines (2 include lines + a
  7-line member/comment block; the call-site edit was a net +1 line).

## 6. Deferred to later phases (not this phase's job)

- No full build / full `ctest` regression run (Phase 4's own job).
- No new Tier-1 unit test file for
  `AtmosphereAerialPerspectiveSkyPurityValidation` itself — this is a Tier-2,
  `Renderer`/`RenderGraph`-dependent tool (same accepted bucket as
  `AtmosphereTransmittanceLutValidation`/`AtmosphereAerialPerspectiveLutInspection`,
  see `TESTING.md`'s "Tier 2" note) — its correctness was instead verified
  by the live PASS/FAIL/PASS runtime sequence in §4 above, mirroring how the
  two precedent tools were originally verified in their own campaigns.
- No documentation updates (`AGENTS.md`/`README.md`/`TODO.md`) — Phase 4's
  own job per `PHASE0_MASTER_STRATEGY.md`.

## Next step

Phase 4 (`PHASE4_REGRESSION_SAFETY_DOCS_AND_FULL_BUILD.md`, context only —
not implemented in this session) will add a regression test for the
UNCHANGED opaque-geometry path, update `AGENTS.md`/`README.md`/`TODO.md`, and
run a full clean build + full `ctest` regression pass as its own last step.
