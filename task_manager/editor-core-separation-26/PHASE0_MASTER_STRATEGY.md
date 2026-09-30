# PHASE0 — MASTER STRATEGY: "Buffer Roots (KeepBufferOutput) + Blit/Copy Passes (PassKind::Blit)"

Campaign folder: `task_manager/editor-core-separation-26/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

Source design document (read-only, in a SEPARATE repo/folder — never modify it):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-5\BIG_STEP_2_BUFFER_ROOTS_AND_BLIT_PASSES_2026-09-29.txt`
— this is BIG STEP 2 OF 4 of a larger series. BIG STEP 1 (Core/Editor
Separation: PassRecord Debug Metadata Sink) already shipped as
`task_manager/editor-core-separation-25/` — read that campaign's own
`CAMPAIGN_COMPLETION_REPORT.md` once, for background, but nothing in THIS
campaign depends on it. Persistent Resource Cache (Step 3) and GPU Memory
Aliasing (Step 4) are NOT part of this campaign either — no dependency in
any direction. Read the source document IN FULL before starting ANY phase
below — this file and its children summarize/sequence it, they do not
replace it.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE7_*.md`) must be read together with this file. Every child phase must,
at its own start, re-read this file plus the completion report of the phase
immediately before it (`PHASEn-1_COMPLETION_REPORT.md`) — there might be a
clue for continuation, a discovered root cause, or a locked decision that
changes how the next phase must be carried out.

---

## Step 1: The Goal (Where are we going?)

Today, two small, independent, "real missing feature" gaps exist in this
engine's render graph:

**Gap A — Buffer Roots.** `RenderGraphCompiler::Compile()`'s root-marking scan
(`RenderGraphCompiler.cpp`, the `DispatchByKind()` call inside its Step 2
"backward reachability from `finalOutputs`" loop) has exactly one line that
forecloses a `BufferHandle` from EVER being a root:
`[&](BufferHandle) { return false; }`. A compute pass whose only observable
effect is a buffer write, with no in-frame reader (e.g. "fill this buffer
this frame purely for a LATER frame's own `ImportBuffer()` to read"), is
silently culled today, no matter what it declares. This is the exact same
shape of gap `VolumeTextureHandle` had before Atmosphere Scattering Phase 6
closed it via `finalVolumeTextureOutputs`/`KeepVolumeTextureOutput()` — a
proven, already-shipped pattern this gap is a pure, mechanical copy of.

**Gap B — Blit/Copy Passes.** `RenderPassDrawKind::Blit` already exists as a
real, currently completely unused scaffold enumerator, and `PassKind`'s own
doc comment already, deliberately, anticipates a third kind being added
later "without every call site having to re-litigate what `true`/`false`
used to mean." There is today no way for a pass author to ask this engine to
blit/copy/scale one texture into another (`vkCmdBlitImage`) without hand-
writing raw Vulkan inside a `setup`/`execute` pair — a real, missing,
anticipated feature.

When this campaign is done:

1. `RenderGraphBuilder::KeepBufferOutput(BufferHandle)` exists, is idempotent,
   and a buffer passed to it survives `Compile()`'s culling pass even with
   zero in-frame readers — mirroring `KeepVolumeTextureOutput()` exactly.
2. `RenderGraphBuilder::AddBlitPass(name, BlitSpec, ...)` exists as a real,
   official, first-class pass-declaration entry point (mirroring
   `AddRenderPass()`'s own trailing-defaulted-parameter convention) that
   blits/copies/scales one texture into another via `vkCmdBlitImage`, with
   correct barriers, correct color/depth aspect selection, a correctly
   forced filter for any depth-flagged side, and correct Frame Debugger
   labeling — with NO hand-written Vulkan required from the pass author.
3. Every place in the Editor that used to implicitly assume "a pass is either
   Compute, or it must be Graphics" is re-audited and, where that assumption
   was actually load-bearing (not just present), converted to a real,
   exhaustive three-way dispatch — so a `PassKind::Blit` pass is never
   mislabeled, and never silently steals another pass's special-cased
   structural slot (e.g. the GameView view-region walk's own "first
   Graphics-kind pass is RenderOpaque" assumption).
4. A color-to-color blit is proven correct on real, running hardware via a
   live screenshot; a depth-to-depth blit is either proven correct on real
   hardware behind a real, permanent, reusable format-feature-support query,
   or explicitly, honestly documented as shipped-but-unverified — never
   silently assumed to work.
5. Zero changes to `RenderGraphBarrierPlanner.cpp` for either gap (TR1/TR2 of
   the source document) — both gaps reuse existing, unchanged machinery.
   Zero changes to any pre-existing `AddRenderPass()`/`AddPass()`/
   `AddComputePass()`/`WriteTexture()`/`Compile()` call site's compiled
   behavior anywhere in the engine (TR3) — both gaps are strictly additive.

This mirrors TWO already-shipped, already-proven precedents in this exact
codebase: `VolumeTextureHandle`'s own root-set fix (Atmosphere Scattering
campaign, Phase 6) for Gap A, and `PassKind`'s own doc-comment-anticipated
extension point for Gap B. Do not invent a new pattern for either — copy
these two, with the deliberate refinements this campaign's own analysis
below adds (Locked Decisions 1-4).

## Step 2: The Situation (Where are we now?)

Every file this campaign touches was re-read fresh, directly, immediately
before these phase files were written — confirmed, not assumed:

- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`, lines ~485-509 — the
  root-marking scan. Line 498 is the exact line Gap A fixes:
  `[&](BufferHandle) { return false; },` inside a `DispatchByKind()` call.
  `ContainsVolumeTextureHandle()` (lines 28-36, same file) is the EXACT
  shape `ContainsBufferHandle()` must copy.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — `CompiledGraphInput`
  (line ~153) already carries `finalVolumeTextureOutputs` (line 178) as the
  precedent for the new `finalBufferOutputs` field.
  `RenderGraphBuilder::KeepVolumeTextureOutput()` is declared at line 384,
  implemented in `RenderGraphBuilder.cpp` line 194-197 (a one-line
  `push_back` onto `m_finalVolumeTextureOutputs`, line 607) — this exact
  three-line shape (private vector member, public declare method, `Finish()`
  moves it into `CompiledGraphInput`, `RenderGraphBuilder.cpp` line 206) is
  what `KeepBufferOutput()` copies verbatim.
- `PassBuilder::ReadTexture()` (`RenderGraphBuilder.h` line 213) ALREADY has
  a third, trailing, defaulted `bool isDepthResource = false` parameter.
  `PassBuilder::WriteTexture()` (line 271) does NOT — confirmed by direct
  read, this is the exact, single gap Gap B's own prerequisite (PHASE2
  below) closes.
- `ResourceUsage::isDepthResource` (`RenderGraphTypes.h` line 573) and
  `ResourceUsage::ForTexture(handle, access, isDepthResource = false)`
  (line 575) already exist — `WriteTexture()`'s new parameter has
  everywhere it needs to flow to already built and waiting.
- `RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()` (line 157-245) already
  computes `const bool isDepthAccess = TargetsDepthState(usage.access) ||
  usage.isDepthResource;` (line 205) and already correctly selects
  `tex.target.image`/`VK_IMAGE_ASPECT_COLOR_BIT` vs.
  `tex.target.depthImage`/`VK_IMAGE_ASPECT_DEPTH_BIT` (lines 206-217) — this
  is COMPLETELY UNCHANGED by this campaign; it already does exactly what
  Gap B needs, the only missing piece was `WriteTexture()`'s own parameter
  to set `usage.isDepthResource` true for a WRITE (the READ side already
  works via `ReadTexture()`'s existing third parameter).
- `RenderGraphBarrierPlanner.cpp`'s `RequiredStateFor()` (lines 6-122)
  ALREADY maps `ResourceAccess::TransferSrc` →
  `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`/`VK_ACCESS_2_TRANSFER_READ_BIT` and
  `ResourceAccess::TransferDst` →
  `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`/`VK_ACCESS_2_TRANSFER_WRITE_BIT`
  (lines 45-56) — confirmed, TR1 holds: ZERO changes needed to this file
  for either gap.
- `PassKind` (`RenderGraphTypes.h` line 378-381) is `enum class PassKind :
  std::uint8_t { Graphics, Compute };` today — a real, deliberately
  exhaustive-switch-friendly small enum (its own doc comment literally
  anticipates "a future pure-blit/copy pass"). `ToString(PassKind)`
  (`RenderGraphTypes.cpp` lines 67-77) is a genuinely `default:`-less
  exhaustive switch over it — **this function is NOT named anywhere in the
  source document, but it WILL silently become non-exhaustive (a
  `-Wswitch`-class gap, not a hard build error — this repo does not build
  with `-Werror` on its own code, confirmed via a fresh grep of the root
  `CMakeLists.txt`) the instant `PassKind::Blit` is added, and
  `RenderGraphMetadata.cpp` line 38 (`metadata.kind =
  ToString(pass.kind);` — the real backing code for `GET /render_graph`'s
  JSON `"kind"` field) would silently start reporting `"Unknown"` for every
  real Blit pass if this is missed.** This is a genuine gap THIS campaign's
  own direct-code review found, beyond the source document's own text — see
  PHASE3's own required fix.
- `RenderPassDrawKind::Blit` (`RenderGraphTypes.h`) already exists;
  `RenderGraphTypes.cpp`'s `ToString(RenderPassDrawKind)` (line 97-109)
  ALREADY has `case RenderPassDrawKind::Blit: return "Blit";` (line 105-106)
  — nothing to do there. `FrameDebuggerData.cpp`'s
  `GraphicsChildEventLabelFor(rg::RenderPassDrawKind)` (line 331-342)
  ALREADY has `case rg::RenderPassDrawKind::Blit: return "Blit";` (line
  338-339) too — also nothing to do there. Both of these were clearly
  pre-built, waiting, exactly as the source document claims.
- `src/Editor/FrameDebuggerData.cpp` — a fresh, full grep for `PassKind` and
  for the regex `\.kind\s*[=!]=` across all of `src/` (not just
  `src/Editor/`) found **exactly 5** real `PassKind` comparison sites, ALL 5
  inside this one file, and confirmed **zero** anywhere else in `src/`
  (`src/Application/`, `src/Core/`, `src/Renderer/` outside
  `RenderGraph.cpp`/`RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`
  themselves, which TR1/TR2 already say need zero changes) — this satisfies
  the source document's own "re-run this grep before Blit is introduced"
  requirement, done fresh for this campaign:
  1. Line 241, `FindPostGameViewCompositePassExecutionIndex()`:
     `if (pass.kind != rg::PassKind::Compute || ...) { continue; }`
  2. Line 808, the Pre-GameView compute-discovery loop:
     `if (pass.kind != rg::PassKind::Compute) { continue; }`
  3. Lines 870/887, the GameView view-region walk:
     `if (pass.kind == rg::PassKind::Compute) { ...break; }` followed,
     unconditionally, by a comment asserting "`pass.kind ==
     rg::PassKind::Graphics` from here on" and code that acts on that
     assumption — including, on its FIRST iteration, calling
     `BuildRenderOpaqueLeaf()` unconditionally.
  4. Line 950, the Post-GameView compute-discovery loop:
     `if (pass.kind != rg::PassKind::Compute) { continue; }`
  5. Line 1010, the final "Other Render Passes" catch-all sweep:
     `(pass.kind == rg::PassKind::Compute) ? BuildComputeDispatchLeaf(...) :
     BuildGraphicsPassLeaf(...)`.
  **Confirmed by this campaign's own `ask_questions` resolution (see Locked
  Decision 2 below): only sites 2 (Pre-GameView loop), 3 (view-region walk),
  and 5 (catch-all sweep) — the exact 3 the source document itself names —
  are actually converted to a real, exhaustive three-way dispatch. Sites 1
  and 4 are one-way Compute-only FILTERS (`if not Compute, skip/continue`),
  never a disguised "not Compute = Graphics" binary branch — a Blit pass
  reaching either of them is correctly, safely skipped exactly like a
  Graphics pass already is today, with no field read that is Graphics-only.
  PHASE4 below is required to re-verify this exact claim (not just take it
  on faith) and to write the "checked and found already-safe, and why"
  reasoning directly into the audited file, per the user's own explicit
  instruction, so a future re-run of this same grep does not re-litigate
  this from zero.**
- `RenderGraphPassSnapshot` (`RenderGraphSnapshot.h`, line 90) already
  carries `kind`/`category`/`drawKind`/`viewScope`/`renderPassEvent` as
  plain copied-through fields (editor-core-separation-25's own Non-Goal 3 —
  this 5-field shape is NOT touched by this campaign either).
  `BuildPassSnapshot()`/`BuildRenderGraphSnapshot()` need **zero** changes
  for Gap B — a Blit pass's `kind`/`drawKind` flow through the exact same
  path every other pass's already does.
- `src/Renderer/Vulkan/FormatCapabilities.h`/`.cpp` — already exists, already
  has exactly ONE function, `SupportsStorageImageUsage(VkPhysicalDevice,
  VkFormat)` (a two-line body: `vkGetPhysicalDeviceFormatProperties()` +
  one bitmask check) — this is the EXACT sibling shape the new
  `SupportsBlitSrcDst()` helper (PHASE6) copies, in the same file.
  `VulkanDevice` (`src/Renderer/Vulkan/VulkanDevice.h`/`.cpp`) already
  caches TWO device capabilities eagerly in its own constructor exactly this
  way — `m_timestampCapability` (`QueryTimestampCapability()`) and
  `m_supportsDrawIndirectCount` — both exposed via a plain `noexcept`
  getter (`TimestampCapability()`, `SupportsDrawIndirectCount()`). This is
  the precedent `SupportsDepthBlit()` copies (PHASE6) — computed ONCE, in
  `VulkanDevice`'s own constructor (calling the ALREADY-EXISTING
  `PickDepthFormat()` first to learn the real depth format, then querying
  blit-src/blit-dst feature-bit support for exactly that format), never
  re-checked afterward, surfaced through `Renderer` the same way
  `Renderer::DepthFormat()` already forwards `VulkanDevice::
  PickDepthFormat()`.
- `GET /get_texture?texture_name=<name>` and `GET /list_textures`
  (`src/Network/NetworkServer.cpp`, lines 96-193) already exist and can
  fetch ANY currently-"published" named render-graph texture as a real PNG
  — this is what PHASE6's own live color-to-color proof uses; no ImGui
  panel/checkbox is required to satisfy the source document's own B.3
  acceptance bar ("confirmed visually correct... via a real screenshot").
- No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt` change,
  is needed ANYWHERE in this whole campaign — every change is either a new
  member/function added to an EXISTING file, or a new test case added to an
  EXISTING test file (`RenderGraphCompilerTests.cpp`,
  `RenderGraphBuilderTests.cpp`, `RenderGraphTypesTests.cpp`,
  `FrameDebuggerSnapshotBuilderTests.cpp`). Confirmed by direct inspection
  of every file this campaign's 7 phases touch — every one of them already
  exists and is already listed wherever it needs to be.

## Step 3: The Plan (detailed strategy)

This campaign is split into 7 implementation phases, each its own `.md`
file in this same folder, plus this PHASE0 orchestrator.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_BUFFER_ROOTS_KEEPBUFFEROUTPUT.md` | Gap A, in full: `KeepBufferOutput()`, `CompiledGraphInput::finalBufferOutputs`, `ContainsBufferHandle()`, the one-line root-scan fix. Fully independent of every later phase. |
| 2 | `PHASE2_WRITETEXTURE_ISDEPTHRESOURCE_PARAMETER.md` | Gap B's REQUIRED prerequisite: `PassBuilder::WriteTexture()` gains a trailing, defaulted `isDepthResource` parameter, mirroring `ReadTexture()`'s existing one. Zero behavior change for every pre-existing call site. |
| 3 | `PHASE3_BLITSPEC_PASSKIND_AND_PURE_HELPERS.md` | `BlitSpec`, `PassKind::Blit`, `PassRecord::blitCommand`, the `ToString(PassKind)` fix, and 3 new pure, Tier-1-testable helper functions (`ResolveEffectiveBlitFilter()`, `ResolveBlitRegion()`, `IsValidBlitRegion()`) living beside `BlitSpec` — per Locked Decision 1 (below), extracted OUT of the future Vulkan execution branch specifically so they get real, GPU-free unit tests. No pass can be declared as `PassKind::Blit` yet — pure data/type addition only. |
| 4 | `PHASE4_FRAMEDEBUGGER_PASSKIND_EXHAUSTIVENESS_AUDIT.md` | The REQUIRED COMPANION AUDIT — converts exactly the 3 named hazard sites in `FrameDebuggerData.cpp` into a real, exhaustive three-way `PassKind` dispatch, confirms (and documents, in-file) the other 2 grep hits are already safe and why, and adds the Frame-Debugger-tier synthetic-Blit-pass test. MUST land before PHASE5. |
| 5 | `PHASE5_ADDBLITPASS_BUILDER_ENTRYPOINT.md` | `RenderGraphBuilder::AddBlitPass()` — the real, official, first-class pass-declaration entry point, wiring `ReadTexture`/`WriteTexture` internally, stamping `kind`/`drawKind`. Pure data — no Vulkan call yet. Tier-1 tests only. |
| 6 | `PHASE6_BLIT_EXECUTION_AND_LIVE_VALIDATION.md` | The `vkCmdBlitImage` execution branch in `RenderGraph.cpp`; `SupportsBlitSrcDst()` (`FormatCapabilities.h`/`.cpp`) + `VulkanDevice::SupportsDepthBlit()` (the permanent, reusable, cached hardware-support query, per Locked Decision 4); the new, permanent, minimal, Debug-category `BlitValidation` pass (per Locked Decision 3); the live, `GET /get_texture`-driven color-to-color screenshot proof; the depth-to-depth attempt (exercised for real if `SupportsDepthBlit()` reports true on this dev machine, otherwise explicitly documented as shipped-but-unverified). |
| 7 | `PHASE7_FULL_REGRESSION_ACCEPTANCE_AND_CLOSEOUT.md` | Every acceptance-criteria checkbox from the source document's own "COMBINED ACCEPTANCE CRITERIA" section individually re-confirmed with fresh evidence; full clean build; full `ctest` regression; `CAMPAIGN_COMPLETION_REPORT.md`. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

Resolved with the user, via `ask_questions`, BEFORE any phase file below was
written:

1. **`ResolveEffectiveBlitFilter(const BlitSpec&) -> VkFilter`,
   `ResolveBlitRegion(min, max, resolvedExtent) -> ResolvedBlitRegion`, and
   `IsValidBlitRegion(min, max, resolvedExtent) -> bool` are extracted as
   small, pure, Tier-1-testable free functions living beside `BlitSpec`
   (`RenderGraphTypes.h`/`.cpp`), NEVER written inline-only inside
   `RenderGraph.cpp`'s Vulkan execution branch.** Confirmed with the user:
   this mirrors this codebase's own repeated, explicit house convention —
   `FindMismatchedColorAttachmentExtent()`, `TargetsDepthState()`,
   `IsColorAttachmentWriteAccess()`, `DetectRenderPassEventContradictions()`
   — "extract the pure DECISION, keep the live-device call site a thin
   wrapper around it," specifically so a screenshot-only proof can never be
   the sole test coverage for a logic decision (filter-forcing, region
   resolution) that a screenshot is a genuinely weak test for (a wrong
   filter can look "close enough" on some formats; a region edge-case
   nobody happens to point the camera at is invisible to a screenshot
   either way). The execution branch's own debug-assert (source document's
   own "REGION VALIDATION" bullet) calls `IsValidBlitRegion()` directly
   rather than hand-rolling the same comparison inline. This does not
   contradict the source document's own TR5 ("the execution branch is the
   one, official place `BlitSpec::filter` is ever resolved") — TR5 is about
   there being exactly ONE call path from `BlitSpec` to a real
   `vkCmdBlitImage` filter value, which remains true; nothing in TR5 forbids
   that one call path from being a named, independently-testable function.
2. **Of the 5 real `PassKind` comparison sites `FrameDebuggerData.cpp`
   contains (see Step 2 above), only the 3 the source document itself names
   (the Pre-GameView compute-discovery loop, the GameView view-region walk,
   and the final "Other Render Passes" catch-all sweep) are converted to a
   real, exhaustive three-way dispatch.** The other 2
   (`FindPostGameViewCompositePassExecutionIndex()`'s own check, and the
   Post-GameView compute-discovery loop) are confirmed, BOTH by this
   campaign's own pre-analysis AND by PHASE4's own required re-verification,
   to be one-way Compute-only filters that never assume "not Compute =
   Graphics" and never touch a Graphics-only field for anything that isn't
   Compute — converting them would only add speculative, symmetry-only
   `case PassKind::Blit: break;` arms for a hazard that provably cannot
   occur there. PHASE4 must still independently re-confirm this exact claim
   against the ACTUAL code (not just cite this document) before leaving
   either site unconverted, and must write the "checked, found already-safe,
   and why" reasoning directly into `FrameDebuggerData.cpp`'s own comments
   at both sites — so a future re-run of this same grep does not have to
   re-derive this reasoning from scratch.
3. **The live/visual proof for Gap B ships as a real, PERMANENT,
   `RenderPassCategory::Debug`-tagged validation pass (`BlitValidation`,
   PHASE6), verified purely via `GET /get_texture` screenshots — NO ImGui
   panel/checkbox/descriptor-set apparatus is built for it**, unlike the
   heavier `ComputeBlurValidation`/`GBufferValidation` precedent (which
   needed that apparatus because they ALSO wanted a live, in-Editor toggle-
   able display — a requirement Gap B's own acceptance bar, source
   document Section B.3, does not ask for: "confirmed visually correct...
   via a real screenshot" is the whole bar). Tagging it `Debug` (never
   `FrameDebuggerInternal`) means it is a real, honest, permanently visible
   citizen of the Frame Debugger tree, exactly like `GBufferValidation`/
   `ComputeBlurValidation` already are — never hidden, never deleted after
   this campaign closes, so any future regression in `AddBlitPass()` has a
   ready-made, zero-setup way to be re-confirmed.
4. **`SupportsDepthBlit()` is a small, permanent, reusable, CACHED hardware-
   capability query, added to `VulkanDevice` (mirroring
   `SupportsDrawIndirectCount()`'s own exact "query once in the constructor,
   expose via a plain getter" shape), backed by a new, permanent
   `SupportsBlitSrcDst(VkPhysicalDevice, VkFormat)` sibling function added to
   the ALREADY-EXISTING `src/Renderer/Vulkan/FormatCapabilities.h`/`.cpp`
   (mirroring `SupportsStorageImageUsage()`'s own exact shape in that same
   file) — NEVER a one-off diagnostic whose result is only ever written into
   a completion report.** The source document's own wording is a STANDING
   precondition ("must not be exercised by any production pass UNTIL a...
   check has confirmed the target driver genuinely supports it") — GPU
   driver support for a specific depth-format blit is real hardware
   variance across vendors, not a fact that gets permanently "settled" by
   one developer's one test run on one machine. Ideally wired directly into
   PHASE6's own execution-branch debug-assert, so this precondition is
   something the ENGINE ITSELF checks on every future run, on whatever real
   hardware it is running on, not just something a doc says once.

### 3.2 — Why this shape (seven phases, not fewer/more)

- PHASE1 (Gap A) is deliberately first and fully self-contained — it shares
  NO file, NO type, and NO ordering dependency with any of PHASE2-6 (Gap B).
  The source document's own SEQUENCING NOTE explicitly recommends this
  order ("Buffer Roots is the smaller of the two... gives a second,
  independent confirmation that the 'copy an existing pattern' approach
  works cleanly before touching anything Vulkan-call-shaped") — followed
  here exactly. If PHASE1 alone somehow needs to ship without the rest of
  this campaign for any reason, it is safe to do so; nothing later depends
  on it.
- PHASE2 is its own phase, tiny as it is, because it is a genuine,
  independently-compilable, independently-testable PREREQUISITE the source
  document itself calls out by name ("implement in this order" step 1) —
  `AddBlitPass()` (PHASE5) literally cannot correctly express
  `BlitSpec::dstIsDepth` without this landing first, and keeping it as its
  own phase makes its own "every pre-existing `WriteTexture()` call site is
  completely unaffected" claim trivial to verify in isolation (a `git diff`
  touching exactly one function signature plus one implementation line).
- PHASE3 adds `PassKind::Blit` to the enum and `PassRecord::blitCommand` to
  the struct, but — critically — NO code path anywhere in the engine can yet
  construct a real, running `PassKind::Blit` pass (that only becomes
  possible in PHASE5). This ordering is not incidental: it is the source
  document's own explicit "implement in this order" step 3 requirement
  ("REQUIRED COMPANION AUDIT... must land BEFORE step 4 [`AddBlitPass()`]
  ... a not-yet-updated binary check silently mis-happens instead of
  failing to compile") — PHASE3 can safely add the enum value and struct
  field with zero risk precisely because nothing can produce one yet; PHASE4
  (the audit) is what makes it SAFE for PHASE5 to start producing real ones.
- PHASE4 is its own phase, between "add the vocabulary" (PHASE3) and "make it
  producible" (PHASE5), for exactly the ordering reason above — and because
  it is a genuinely different kind of work (Editor-tier tree-building logic
  audit + a Frame-Debugger-tier synthetic-snapshot test) than either of its
  neighbors.
- PHASE5 adds the real, producible `AddBlitPass()` entry point but
  DELIBERATELY stops short of the actual `vkCmdBlitImage` call — every new
  behavior through PHASE5 is pure data (a `PassRecord` gets the right
  `reads`/`writes`/`kind`/`drawKind`/`blitCommand` shape), so every one of
  its own tests is a Tier-1, GPU-free test, keeping the phase that finally
  DOES touch a live `VkCommandBuffer` (PHASE6) isolated and easy to bisect
  if something goes wrong.
- PHASE6 is, by a wide margin, the single highest-risk phase in this
  campaign — the first and only phase that adds a real Vulkan call, the
  first and only phase that needs a live, running Editor + real GPU +
  network round-trip to verify, and the phase that adds the permanent
  `SupportsDepthBlit()` capability query plus the permanent `BlitValidation`
  pass. Kept as ONE phase (not split further) because the execution branch,
  the capability query it conditionally gates on, and the pass that proves
  both work are not independently meaningful to verify apart from each
  other — per Locked Decision 8 of the editor-core-separation-25 precedent
  this campaign continues, PHASE6 is the flagged candidate most likely to
  need a `dispatch_sub_agent`-driven self-double-check before its own
  completion report (see Rule 4 below).
- PHASE7 is always last, matching every prior campaign in this codebase's
  own history — full regression + the literal "COMBINED ACCEPTANCE
  CRITERIA" checklist tick-through, once, at the end, never spread across
  phases.

### 3.3 — Rules (apply to every phase, no exception)

1. **Every phase must, at its start, use `ask_questions` for any genuine
   ambiguity it personally discovers** beyond what this file and its own
   phase file already resolve. **Every task an implementation phase itself
   delegates must ALSO be instructed to use `ask_questions`** for its own
   ambiguities — this applies recursively, with no exception, to every
   `delegate_task`/`dispatch_sub_agent` call made anywhere in this campaign.
2. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any NEW
   diagnostic or permanent code in this campaign** (the one PRE-EXISTING
   `std::fprintf(stderr, ...)` inside `RenderGraphCompiler.cpp`'s
   `DetectRenderPassEventContradictions()` machinery is untouched, pre-
   existing code — do not copy its style for anything NEW). Always
   `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR` (`src/Core/Logging.h`),
   retrieved via `GET /get_logs` (`gte_send_request`) — this is a hard,
   repo-wide rule (`AGENTS.md`), not new to this campaign.
3. **Never run a full clean build or full `ctest` regression pass except in
   PHASE7.** Every other phase uses an INCREMENTAL build (`cmake --build
   build`) as its compile-check gate, plus a TARGETED `ctest -R <filter>`
   for whatever new/changed Tier-1 test(s) that phase itself owns. Use
   `run_app_background`/`gte_send_request`/`stop_app_background` for any
   live, HTTP-driven debugging a phase genuinely needs (`GET /get_logs`,
   `GET /render_graph`, `GET /get_texture`, `GET /list_textures`,
   `GET /get_swapchain`) — never a blind guess. Always `stop_app_background`
   whatever you `run_app_background`'d, every time, before ending the phase.
4. **Implementation-phase agents (i.e. whoever is actually executing PHASE1-7)
   may ONLY use `dispatch_sub_agent` to delegate any piece of their own
   work — NEVER `delegate_task`.** `delegate_task` is reserved exclusively
   for the orchestration layer (the agent sequencing PHASE1 → PHASE2 → ...
   → PHASE7 as whole, separate task steps) — it must never be called from
   inside an already-running implementation phase. Use `dispatch_sub_agent`
   freely for a self-contained side-quest (diagnosing a build failure, a
   targeted grep-and-report, a git operation) OR to independently
   double-check this phase's OWN just-finished, large piece of work before
   writing its completion report (PHASE6 is this campaign's own flagged
   heaviest/highest-risk phase — the most likely candidate). Such a
   `dispatch_sub_agent` double-check must NEVER create its own separate
   report file — its final report comes back inline, and the ORIGINAL phase
   still writes the one `PHASEn_COMPLETION_REPORT.md`.
5. **Every phase that changes `gte_core`/`gte_editor`-tier logic must add or
   extend a Tier-1 test wherever the underlying problem allows it**
   (`AGENTS.md`'s "Testability & Regression Safety" section) — every phase
   file below spells out its own exact required test cases.
6. **Every phase must end with**: an incremental compile check succeeding, a
   targeted `ctest` pass for that phase's own new/changed tests, a `.md`
   completion report (`PHASEn_COMPLETION_REPORT.md`) written into this same
   folder, and a git commit (`git_add` + `git_commit`) covering both the
   code change and the report.
7. **No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt`
   change, is expected anywhere in this campaign** (see Step 2's own closing
   bullet above). If any phase discovers it genuinely needs a new file or a
   CMake list edit, STOP and use `ask_questions` before proceeding — that
   would mean this campaign's own understanding of the codebase has a gap.
8. **`TR3` ("every existing call site compiles and behaves unmodified") is
   the one hard constraint that makes this whole campaign safe.** Every
   pre-existing `AddRenderPass()`/`AddPass()`/`AddComputePass()`/
   `WriteTexture()`/`Compile()` call site across the engine keeps compiling,
   completely unmodified, for the whole campaign — if any phase discovers a
   call site that would need to change, STOP and use `ask_questions` before
   proceeding.
9. **Every phase must re-read this file's Step 2 "Situation" section and its
   own phase file's exact line-number citations, then re-confirm each one
   against the ACTUAL current file before relying on it** — line numbers
   drift as earlier phases land real edits; a citation that was correct when
   this document was written may be off by a few lines by the time a later
   phase reads it. Re-search (`search_in_dir`/`read_line`), never assume.

### 3.4 — Definition of Done for the whole campaign

Identical to the source design document's own "COMBINED ACCEPTANCE
CRITERIA" section. PHASE7 restates the full checklist and ticks every box
with its own fresh evidence (code inspection quotes, build output, `ctest`
output, and the live screenshot/`GET /get_texture` evidence for both the
color-to-color proof and the depth-to-depth attempt, whichever way it
resolves). When this is green, `editor-core-separation-26` is closed for
good, and BIG STEP 3 of 4 (Persistent Resource Cache) may begin as its own,
later, separate campaign.
