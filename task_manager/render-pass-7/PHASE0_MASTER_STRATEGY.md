# render-pass-7 — Core Campaign 1: De-hardcode `RenderPassCategory` — MASTER STRATEGY

**Orchestrator document.** Every child phase (`PHASE1`..`PHASE5`) below reports back here
conceptually — read this file FIRST, always, before opening any child phase file. This
campaign implements exactly ONE item from
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-pass\CORE_EXPANSION_STRATEGY_v2.md`,
Section 3, **"Core Campaign 1 — De-hardcode `RenderPassCategory`"**. Nothing else from that
document is in scope. Do not implement Campaigns 2-7. Do not touch anything under
`src/Application/` beyond the exact call sites this document enumerates.

---

## Step 1 — The Goal (Where are we going?)

Today, `src/Renderer/RenderGraph/RenderGraphTypes.h` — a Core (Layer 1) file that is supposed
to be feature-blind — hardcodes two Layer-2 (plugin) feature names directly into its own
`RenderPassCategory` enum:

```cpp
enum class RenderPassCategory : std::uint8_t {
    General,
    AtmosphereLut, // <-- names the Atmosphere Scattering plugin
    GpuSkinning,   // <-- names the GPU Vertex Skinning plugin
    Debug,
};
```

**The goal of this campaign is to make it structurally impossible for a Core file to ever
need to name a specific Layer-2 feature again**, while keeping the Frame Debugger's visible
tree output (the "Compute LUT" heading Atmosphere passes get grouped under today) **exactly,
byte-for-byte unchanged**. Concretely, when this campaign is done:

1. `RenderPassCategory` has exactly two enumerators left: `General` and `Debug` — both
   genuinely Core-level concepts ("no special grouping" / "Editor-internal debug pass"),
   never a named plugin.
2. A brand-new Layer-2 module (a hypothetical future `ShadowMap` plugin) can get its own
   dedicated "grouped under its own heading in the Frame Debugger" treatment — exactly the
   treatment Atmosphere already gets today — **by editing only its own files**, never
   `RenderGraphTypes.h`, never any other Core file.
3. The already-existing, already-designed-but-never-wired `RenderPassTagMask` /
   `RenderPassTag` mechanism (`RenderPipeline.h`) is what makes this possible — this campaign
   finishes wiring it end-to-end instead of inventing a second, parallel mechanism.
4. The Frame Debugger's Game View tree, captured live over the network HTTP API, shows
   **identical** grouping (same headings, same pass placement, same order) before and after
   this campaign, for both real production consumers this affects (Atmosphere LUT passes, GPU
   Skinning dispatches).

---

## Step 2 — The Situation (Where are we now? — concrete evidence, already read from source)

This section is the product of directly reading the real, current source tree (not a guess).
Every fact below was confirmed by opening the actual file before this document was written.

### 2.1 The violation itself

`RenderGraphTypes.h` (lines ~387-392) defines `RenderPassCategory` with the two feature-named
enumerators shown in Step 1. `RenderGraphTypes.cpp`'s `ToString(RenderPassCategory)` has a
matching 4-case switch. `PassRecord` (same file, ~line 747) and `RenderGraphPassSnapshot`
(`RenderGraphSnapshot.h`, ~line 111) both carry a `RenderPassCategory category` field, copied
through in `RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()` (line 94).

### 2.2 Every real call site that sets `RenderPassCategory::AtmosphereLut` or `::GpuSkinning` today

Confirmed by `search_in_dir` across `src/`, exactly these 7 real production call sites (plus
one already-`General` sibling call that needs NO change) exist:

| # | File | Approx. line | Pass name | Category set today |
|---|------|-----|-----------|---------------------|
| 1 | `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` | ~237 | `AtmosphereTransmittanceLutPass` | `AtmosphereLut` |
| 2 | `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` | ~335 | `AtmosphereMultiScatteringLutPass` | `AtmosphereLut` |
| 3 | `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` | ~475 | `AtmosphereSkyViewLutPass` | `AtmosphereLut` |
| 4 | `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` | ~617 | `AtmosphereAerialPerspectiveVolumePass` | `AtmosphereLut` |
| 5 | `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` | ~972 | `AtmosphereAerialPerspectiveVolumeDebugSlicePass` | `AtmosphereLut` |
| 6 | `src/Application/Application.cpp` | ~452 | `"GpuSkinning"` `RenderPipeline` provider (`RenderPassDesc.legacyCategory`) | `GpuSkinning` |
| 7 | `src/Application/RenderPasses.cpp` | ~409 | `AddGpuSkinningPasses()` free function (a direct-render-only fallback path, still live, called from `Application.cpp` line ~1031) | `GpuSkinning` |

(Sibling call, NO change needed: `AtmosphereLutRenderer.cpp` ~line 784,
`AtmosphereAerialPerspectiveCompositePass`, already `RenderPassCategory::General` — leave it
alone.)

**Every other real `AddRenderPass()` call site in the engine already uses `General` or
`Debug`** (RenderOpaque, DrawSkyBackground, Present, the N Frame-Debugger-replay passes,
ComputeBlurValidation, GBufferValidation) — none of those need any change for this campaign.

### 2.3 The ONLY real consumer of `category` for anything beyond pass-through storage

`src/Editor/FrameDebuggerData.cpp`, function `BuildRealFrameDebuggerSnapshot()`:

- Line ~775: `if (pass.category == rg::RenderPassCategory::AtmosphereLut) { computeLutGroup } else { preGameViewGroup }`
  — the ONLY place that groups a pre-GameView compute pass under the "Compute LUT" heading
  vs. the generic "Compute Dispatches (Pre-GameView)" fallback heading. (Note: GPU-Skinning-
  category passes already fall into the `else`/fallback bucket today — they were NEVER
  specially grouped, `GpuSkinning` only ever existed to be *excluded* from the `AtmosphereLut`
  branch.)
- Line ~828: `pass.category == rg::RenderPassCategory::Debug` — excludes Frame-Debugger-
  internal replay/validation passes from the real view-region tree walk. **This check is
  OUT OF SCOPE — `Debug` is a permanent, genuinely Core-level value and is never touched by
  this campaign.**
- The post-GameView loop (lines ~872-893) does **not** branch on `category` at all today —
  confirmed by reading it; nothing there needs to change.

Every other hit for `RenderPassCategory` in `src/` (`RenderGraphBuilder.h`,
`RenderPipeline.h`, `RenderPasses.h`, `GBufferValidation.*`, `ComputeBlurValidation.cpp`,
doc comments) is either a pass-through storage field, a doc comment, or an already-`General`/
`Debug` call site — enumerated exhaustively per-phase below, nothing is missed.

### 2.4 A real, PRE-EXISTING bug this campaign must also fix (found while reading the code, not asked for by the source document, but required to make the fix actually work end-to-end)

`RenderPipeline.h`'s `RenderPassDesc` already has a `RenderPassTagMask tags = 0;` field
(design doc Section 7's own vocabulary), but `RenderPipeline::DeclareOnePhase()`'s own
`builder.AddRenderPass(...)` call **never passes `desc.tags` through** — it is silently
dropped every single frame, for every provider, today. `RenderGraphBuilder::AddRenderPass()`
itself has no `tags` parameter at all yet. This means the tag mechanism the source document
assumes is "already partly proven" is, in fact, **completely inert** for anything routed
through `RenderPipeline` (i.e. the `"GpuSkinning"` provider) — it only looks proven for the
Atmosphere passes because those bypass `RenderPipeline` and call `builder.AddRenderPass()`
directly. **PHASE1 below fixes this as a real, necessary prerequisite** — Campaign 1 cannot
land without it, since the `"GpuSkinning"` provider is the one production pass that MUST
carry a real tag through `RenderPipeline`, not through a direct builder call.

### 2.5 Where `RenderPassTag` / `RenderPassTagMask` live today, and why that has to change

Both are currently defined inside `RenderPipeline.h` (lines ~121-131). `PassRecord`
(`RenderGraphTypes.h`) and `RenderGraphPassSnapshot` (`RenderGraphSnapshot.h`) — both lower,
more-Core files that `RenderPipeline.h` itself `#include`s — need to gain a `tags` field of
this exact type. A lower file cannot depend on a higher one, so the type itself must move
down into `RenderGraphTypes.h` (see PHASE1) — mirroring the EXACT precedent already set by
`RenderPassEvent`, whose own doc comment in `RenderPipeline.h` explains: *"RenderPassEvent
ITSELF is the one exception to 'every new PHASE1 type lives in this file' — it lives in
RenderGraphTypes.h instead, next to PassRecord/RenderGraphPassSnapshot, which it is also
threaded onto."* `RenderPassTagMask`/`RenderPassTag` now need identical treatment, for the
identical reason.

---

## Step 3 — The Plan (child phases)

Five implementation phases, in strict dependency order (each phase's own file has full
super-detail — this is only the index/sequencing rationale):

- **`PHASE1_TAG_VOCABULARY_AND_THREADING.md`** — Relocate `RenderPassTag`/`RenderPassTagMask`
  into `RenderGraphTypes.h`; add `tags` field to `PassRecord`/`RenderGraphPassSnapshot`; add a
  new trailing `tags` parameter to both `RenderGraphBuilder::AddRenderPass()` overloads; fix
  the dead-field bug in `RenderPipeline::DeclareOnePhase()` (Step 2.4). Pure plumbing, zero
  behavior change (tag defaults to 0 everywhere, nothing reads it yet). Must land first —
  every later phase depends on this type actually flowing end-to-end.
- **`PHASE2_PASS_GROUP_REGISTRY.md`** — Add the new, generic Core facility the source
  document calls for: `RegisterPassGroupLabel()` / a lookup API, in a new
  `RenderPassGroupRegistry.h/.cpp` pair. Pure, additive, zero real consumers wired yet (mirrors
  this codebase's own established "vocabulary first, wiring later" discipline). Depends on
  PHASE1's `RenderPassTag`/`RenderPassTagMask` relocation.
- **`PHASE3_DEHARDCODE_CATEGORY_AND_MIGRATE_CALL_SITES.md`** — The actual de-hardcoding: trim
  `RenderPassCategory` to `{General, Debug}`; create the two new Layer-2 tag-vocabulary
  headers (Atmosphere's own, GPU Skinning's own); migrate all 7 real call sites (Step 2.2);
  register the "Compute LUT" label once, from Atmosphere's own code. Depends on PHASE1+PHASE2.
- **`PHASE4_FRAME_DEBUGGER_GENERIC_GROUPING.md`** — Rewrite `FrameDebuggerData.cpp`'s
  tree-grouping logic (Step 2.3) to consume the generic registry instead of switching on the
  now-deleted enumerators, with a **byte-identical output** requirement, plus a NEW test
  proving the mechanism is genuinely generic (a synthetic, test-only tag/label pair). Depends
  on PHASE3 (the enum must already be trimmed, and real tags must already be flowing, before
  this phase's tests can even compile).
- **`PHASE5_VERIFICATION_AND_CAMPAIGN_COMPLETION.md`** — Full regression pass (`ctest`), a
  live HTTP-driven Frame Debugger smoke test via `gte_send_request` (open/enable/capture,
  visually confirming grouping is unchanged), `AGENTS.md` update, and the campaign's own
  `CAMPAIGN_COMPLETION_REPORT.md`. This is the ONLY phase allowed to run a full build/full
  regression test, per this campaign's own process rules.

### Sequencing rationale

PHASE1 must go first because everything downstream needs a working `tags` field that
actually survives from a call site through to `RenderGraphPassSnapshot`. PHASE2 must precede
PHASE3 because PHASE3's Atmosphere self-registration call needs the registry API to already
exist. PHASE3 must precede PHASE4 because PHASE4's rewritten `FrameDebuggerData.cpp` logic
and its tests need to compile against the ALREADY-TRIMMED `RenderPassCategory` enum and the
ALREADY-REAL tag values flowing through real passes — writing PHASE4 first would mean writing
against enum values that get deleted one phase later. PHASE5 is last because it is the only
phase allowed to run a full, slow regression pass.

### What stays permanently unchanged (explicit non-goals, mirrors the source document's own Section 6)

- No new `RenderPassCategory` enumerators of any kind, ever, for anything (a future plugin
  uses a tag, never a category value) — that is the entire point of this campaign.
- No change to `ViewScope`, `RenderPassDrawKind`, `RenderPassEvent`, or any compiler/barrier
  behavior — this campaign is a pure Frame-Debugger-visible-metadata refactor plus one
  genuine dead-code bug fix (Step 2.4). `RenderGraphCompiler`/`RenderGraphBarrierPlanner`/
  `RenderGraph::Execute()` read none of `category`/`tags` today and must continue to read
  none of them after this campaign.
- No migration of Atmosphere/GPU Skinning into fully clean Layer-2 modules (out of scope per
  the source document's own Section 1.3/6) — this campaign only removes the ONE concrete,
  evidenced Core-file violation their pass declarations trigger today.
- No Core Campaigns 2-7 from the source document.

### Cross-cutting rule every phase must follow

Per this codebase's own established `AddRenderPass()` convention: any new parameter is added
as a TRAILING, DEFAULTED parameter, appended at the very end of the parameter list, never
inserted in the middle — this is what keeps every pre-existing call site compiling
unmodified. Any new struct field is likewise appended at the END of the struct. Every new
exhaustive `switch` in this codebase has **no `default:` case, ever** (see `AGENTS.md`) —
`ToString(RenderPassCategory)` must keep this discipline after being trimmed to 2 cases.
