# PHASE1 — RenderGraph Compute-Dispatch Choke-Point Infrastructure

## Parent -> `PHASE0_MASTER_STRATEGY.md`, read it first.

Branch: `feature/frame-debugger-impl`
Risk level: **HIGH** — this is the campaign's single riskiest phase (see PHASE0's Step 3.5). Touches core
`src/Renderer/RenderGraph/` files consumed by every render pass in the engine, graphics and compute alike.
Take the "STANDING RULE" at the top of `PHASE0_MASTER_STRATEGY.md` especially seriously here: if anything
about the exact touch points below looks different from what this document describes once you actually open
the live files, STOP and re-read the live source before proceeding — do not assume this document's line
numbers are still exactly current.

## Step 1: The Goal (Where are we going?)

Give `RenderGraphBuilder::AddComputePass()` a REAL, structurally-tracked meaning. Today it is, by its own
doc comment, "a thin, PURELY COSMETIC alias" of `AddPass()` — calling it vs. calling plain `AddPass()`
produces byte-for-byte identical `PassRecord`s. After this phase:

1. Every `PassRecord` built via `AddComputePass()` carries `isComputePass == true`; every `PassRecord` built
   via plain `AddPass()` carries `isComputePass == false` (the default). This flag survives all the way
   through `RenderGraphCompiler::Compile()` into `RenderGraphSnapshot::RenderGraphPassSnapshot::isComputePass`,
   for BOTH surviving and culled passes (a culled compute pass must still truthfully report
   `isComputePass == true` — only its `stats` stays defaulted, exactly like every other culled-pass field
   already does).
2. `RenderGraphPassSnapshot` also gains two new PARALLEL vectors, `readKinds`/`writeKinds`
   (`std::vector<ResourceKind>`, same length/order as the existing `readNames`/`writeNames`), so any
   consumer can tell "this read/write name refers to a 2D texture" from "...a buffer" from "...a volume
   texture" without guessing or probing multiple registries.
3. Nothing about actual rendering behavior changes AT ALL — no barrier, no culling decision, no
   `vkCmdBeginRendering`/`vkCmdDispatch` call is touched. This is pure, additive, read-only-by-everyone-else
   metadata, verified by the fact that every single existing test in
   `tests/Renderer/RenderGraph/*Tests.cpp` must still pass completely unmodified (only NEW tests are added
   this phase — no existing RenderGraph test assertion should need to change).

## Step 2: The Situation (Where are we now?)

Confirmed live in this repository (re-verify line numbers/exact text at implementation time):

- `src/Renderer/RenderGraph/RenderGraphTypes.h`'s `struct PassRecord` (~line 392) has fields `name`,
  `reads`, `writes`, `isCulled`, `execute`, `colorClearValue`, `depthClearValue` — **no compute-vs-graphics
  flag of any kind**.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`'s `AddComputePass()` (~line 387) is:
  ```cpp
  template <typename SetupFn, typename ExecuteFn>
  void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
  {
      AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
  }
  ```
  — genuinely zero-behavioral-difference today, exactly as its own doc comment says.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h`'s `struct RenderGraphPassSnapshot` (~line 63) has
  `name`, `isCulled`, `readNames`, `writeNames`, `stats` — no compute flag, no per-entry kind tag.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()` (~line 30) is where
  `readNames`/`writeNames` get filled from `pass.reads`/`pass.writes` via the local `ResourceUsageName()`
  helper — this is where the new `isComputePass`/`readKinds`/`writeKinds` copy-through must be added.
- **NEWLY CONFIRMED by this double-check pass — a pre-existing, unrelated bug this phase must ALSO fix (see
  3.4b in the Plan below), since it lives in the exact function this phase's own `readKinds`/`writeKinds`
  depend on to be meaningful:** the SAME `ResourceUsageName()` helper above NEVER resolves a
  `ResourceKind::VolumeTexture` usage's real name — it is still the old, plain two-way
  `if (kind == Texture) {...} else {assume Buffer}` shape that `RenderGraphCompiler.cpp`/`RenderGraph.cpp`
  both explicitly document having already been audited and converted away from when `VolumeTexture` was
  first added (Atmosphere Scattering campaign, Phase 2) — this one file was missed by that audit. Confirmed
  live: `AtmosphereLutRenderer.cpp`'s `AddAerialPerspectiveVolumePass()`'s `WriteVolumeTexture()` write and
  `AddAerialPerspectiveCompositePass()`'s `ReadVolumeTexture()` read BOTH already produce an EMPTY-STRING
  name in `writeNames`/`readNames` today, in production, right now — an existing, independent display bug
  in the already-shipped `Editor/Panels/RenderGraphPanel.cpp` "Render Graph" panel too, not something this
  phase introduces, but this phase must not let its own new `readKinds`/`writeKinds` compound it.
- `src/Renderer/RenderGraph/RenderGraphTypes.h`'s `enum class ResourceKind : std::uint8_t { Texture, Buffer,
  VolumeTexture };` (~line 294) already exists and is already the tag carried per-entry inside
  `ResourceUsage::kind` (~line 330) — `pass.reads[i].kind`/`pass.writes[i].kind` are ALREADY exactly the
  values `readKinds`/`writeKinds` need to copy out, nothing new needs to be invented for the kind values
  themselves.
- Confirmed via a full-repository grep: only two production files consume `RenderGraphPassSnapshot::readNames`/
  `writeNames` today — `src/Editor/FrameDebuggerData.cpp` and `src/Editor/Panels/RenderGraphPanel.cpp` —
  and both only ever join them into a display string. Adding two new PARALLEL vectors alongside the
  existing ones (never renaming/retyping the existing fields) is confirmed safe and non-breaking for both.
- Confirmed via grep: exactly 8 real production `AddComputePass()` call sites exist today (see PHASE0's
  Step 2.2 for the full list) — GPU Skinning (`Application/RenderPasses.cpp`), Compute Blur Validation
  (`Editor/ComputeBlurValidation.cpp`), and 6 atmosphere passes (`Renderer/Atmosphere/AtmosphereLutRenderer.cpp`).
  Every one of them will automatically start reporting `isComputePass == true` the moment this phase lands,
  with **zero changes needed to any of those 8 call sites themselves**.

## Step 3: The Plan

### 3.1 `RenderGraphTypes.h` — `PassRecord::isComputePass`

Add one new field to `struct PassRecord`, right next to the existing `isCulled` (same section, same style
of doc comment explaining what writes it and what reads it):

```cpp
// frame-debugger-5 campaign, PHASE1 - true for every pass declared via
// RenderGraphBuilder::AddComputePass() (see that method's own updated doc
// comment, RenderGraphBuilder.h); false (the default) for a pass declared
// via plain AddPass() - i.e. every real graphics/draw pass in this engine
// today (e.g. "GameView"). This is PURELY DESCRIPTIVE metadata: nothing in
// RenderGraph.cpp/RenderGraphCompiler.cpp reads this field at all - it
// exists solely so a downstream CONSUMER (RenderGraphSnapshot.h's own
// RenderGraphPassSnapshot::isComputePass, read by the Editor's Frame
// Debugger - see FrameDebuggerData.cpp) can generically discover "which
// passes that ran this frame were compute dispatches" without needing to
// already know every compute pass's exact string name in advance.
bool isComputePass = false;
```

### 3.2 `RenderGraphBuilder.h` — `AddComputePass()` actually sets the flag

Replace the existing `AddComputePass()` body (and REWRITE its own doc comment — it must no longer claim to
be "PURELY COSMETIC", that claim becomes false the moment this lands):

```cpp
// frame-debugger-5 campaign, PHASE1 - UPDATES this method's own former
// "purely cosmetic" claim: this is now the ONE place in the whole engine
// that marks a pass as a real compute dispatch (PassRecord::isComputePass -
// see RenderGraphTypes.h). Still otherwise behaviorally identical to plain
// AddPass() - a pass's actual barrier/attachment/dispatch behavior is still
// entirely determined by what it declares in `reads`/`writes` via `setup`,
// never by which entry point created it; this method ONLY additionally
// stamps one bool. Every real compute pass in this engine already calls
// this method (not plain AddPass()) - see PHASE0_MASTER_STRATEGY.md's Step
// 2.2 for the full, confirmed list - so this one flag alone is enough to
// make every one of them automatically, generically discoverable by any
// future consumer (the Editor's Frame Debugger, frame-debugger-5 PHASE2,
// is the first one).
template <typename SetupFn, typename ExecuteFn>
void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
{
    AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    m_passes.back().isComputePass = true;
}
```

`m_passes.back()` is always safe here — `AddPass()` (which this calls first) unconditionally
`push_back`s exactly one new `PassRecord` before returning, so `m_passes` is guaranteed non-empty and its
last element is guaranteed to be the one `AddPass()` just built.

### 3.3 `RenderGraphSnapshot.h` — `RenderGraphPassSnapshot` grows three new fields

```cpp
struct RenderGraphPassSnapshot {
    std::string name;
    bool isCulled = false;
    std::vector<std::string> readNames;
    std::vector<std::string> writeNames;

    // frame-debugger-5 campaign, PHASE1 - true for a real compute dispatch
    // (see PassRecord::isComputePass's own doc comment, RenderGraphTypes.h)
    // - copied straight through for BOTH a surviving AND a culled pass (a
    // culled compute pass must still truthfully report this - only its
    // `stats` below stays at its own default for a culled pass, per this
    // struct's own pre-existing convention).
    bool isComputePass = false;

    // frame-debugger-5 campaign, PHASE1 - PARALLEL to readNames/writeNames
    // above (same index, same length) - which ResourceKind (RenderGraphTypes.h)
    // each entry actually is, so a consumer never has to guess/probe
    // multiple registries to tell "this name is a real 2D texture" from
    // "...a buffer" from "...a volume texture". A pass with, e.g., 2 reads
    // and 1 write has readKinds.size() == 2 and writeKinds.size() == 1,
    // always exactly matching readNames.size()/writeNames.size().
    std::vector<ResourceKind> readKinds;
    std::vector<ResourceKind> writeKinds;

    PassGpuStats stats;
};
```

(`ResourceKind` is already `#include`d transitively via `RenderGraphBuilder.h` -> `RenderGraphTypes.h`,
already included at the top of this file — confirm this at implementation time; add an explicit include if
it is not already visible.)

### 3.4 `RenderGraphSnapshot.cpp` — `BuildPassSnapshot()` copies the new fields through

```cpp
RenderGraphPassSnapshot BuildPassSnapshot(const PassRecord& pass, const CompiledGraphInput& input, bool isCulled,
    const std::function<PassGpuStats(const char*)>& statsLookup)
{
    RenderGraphPassSnapshot snapshot;
    snapshot.name = pass.name != nullptr ? pass.name : "";
    snapshot.isCulled = isCulled;
    snapshot.isComputePass = pass.isComputePass; // frame-debugger-5, PHASE1

    snapshot.readNames.reserve(pass.reads.size());
    snapshot.readKinds.reserve(pass.reads.size());          // frame-debugger-5, PHASE1
    for (const ResourceUsage& usage : pass.reads) {
        snapshot.readNames.push_back(ResourceUsageName(usage, input));
        snapshot.readKinds.push_back(usage.kind);           // frame-debugger-5, PHASE1
    }

    snapshot.writeNames.reserve(pass.writes.size());
    snapshot.writeKinds.reserve(pass.writes.size());        // frame-debugger-5, PHASE1
    for (const ResourceUsage& usage : pass.writes) {
        snapshot.writeNames.push_back(ResourceUsageName(usage, input));
        snapshot.writeKinds.push_back(usage.kind);          // frame-debugger-5, PHASE1
    }

    if (!isCulled && statsLookup) {
        snapshot.stats = statsLookup(pass.name);
    }

    return snapshot;
}
```

Besides `BuildPassSnapshot()` itself, this phase ALSO fixes `ResourceUsageName()` (see 3.4b immediately below)
— a pre-existing, unrelated bug this phase's own new `readKinds`/`writeKinds` metadata would otherwise make
actively misleading rather than merely incomplete. The two loops that build `snapshot.resources` further down
in this same file are untouched.

### 3.4b `RenderGraphSnapshot.cpp` — REQUIRED companion fix: `ResourceUsageName()` never resolves a
`VolumeTexture` usage's name today (confirmed live, pre-existing bug — not introduced by this phase, but this
phase must fix it, since it lives in the exact function `readKinds`/`writeKinds` above depend on to be
meaningful)

Re-verified live — `ResourceUsageName()` (this same file, immediately above `BuildPassSnapshot()`) is still:

```cpp
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    if (usage.kind == ResourceKind::Texture) {
        if (usage.texture.index < input.textureNames.size()) {
            const char* name = input.textureNames[usage.texture.index];
            return name != nullptr ? name : "";
        }
        return "";
    }
    if (usage.buffer.index < input.bufferNames.size()) {
        const char* name = input.bufferNames[usage.buffer.index];
        return name != nullptr ? name : "";
    }
    return "";
}
```

This is a plain two-way `if (kind == Texture) {...} else {assume Buffer}` shape — **the exact hazard
`RenderGraphCompiler.cpp` and `RenderGraph.cpp` both explicitly document having already audited and converted
away from**, at the time `ResourceKind::VolumeTexture` was first added (Atmosphere Scattering campaign, Phase
2 — see either file's own "IMPORTANT: every place that branches on ResourceKind... was audited and converted
to a real three-way branch... BEFORE this enumerator was added" comment, `RenderGraphTypes.h`). This one file
was missed by that audit. A `VolumeTexture`-kind `ResourceUsage` always has `usage.buffer.index == kInvalidIndex`
(`0xFFFFFFFF`, its own default) — so `usage.buffer.index < input.bufferNames.size()` is always false, and this
function silently falls through to `return ""`.

**Confirmed live, with two REAL, currently-shipping production passes silently affected today, right now,
completely independent of this whole campaign:**

- `AtmosphereLutRenderer.cpp`'s `AddAerialPerspectiveVolumePass()` (~line 594) calls
  `pass.WriteVolumeTexture(outputHandle, ...)` — its `RenderGraphPassSnapshot::writeNames[0]` is an EMPTY
  STRING today, not the volume's real name (e.g. `"AtmosphereAerialPerspectiveVolume_GameView"`).
- `AtmosphereLutRenderer.cpp`'s `AddAerialPerspectiveCompositePass()` (~line 757) calls
  `pass.ReadVolumeTexture(aerialPerspectiveVolumeHandle, ...)` — its corresponding `readNames` entry is
  likewise always an empty string today.
- This means the ALREADY-SHIPPED "Render Graph" Editor panel (`Editor/Panels/RenderGraphPanel.cpp`,
  `JoinNames(pass.readNames)`/`JoinNames(pass.writeNames)`) has always shown a blank Reads/Writes entry for
  these two real rows, for as long as `ResourceKind::VolumeTexture` has existed — an independent, pre-existing,
  user-visible display bug this phase happens to be perfectly positioned to fix as a side effect, since it
  already touches this exact function for an unrelated reason.

Left unfixed, this phase's own new `readKinds`/`writeKinds` become actively MISLEADING, not just incomplete: a
consumer (PHASE2's planned `BuildComputeDispatchLeaf()`) would correctly learn "this write is a
`VolumeTexture`" from `writeKinds[i]`, then look at `writeNames[i]` for the volume's actual name and find an
empty string — and PHASE4's planned volume-texture ray-march preview
(`RenderGraphDebugVolumeTextureRegistry::DebugVolumeTextureSnapshotFor(name)`) depends on exactly that name
being real and non-empty to look the volume up at all. **Fix `ResourceUsageName()` to a real, exhaustive,
`default:`-less three-way `switch (usage.kind)`, in THIS phase, mirroring `RenderGraphCompiler.cpp`/
`RenderGraph.cpp`'s own established convention** (so a future fourth `ResourceKind` fails to compile here too,
until this function is updated to match):

```cpp
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    std::string name;
    switch (usage.kind) {
    case ResourceKind::Texture:
        if (usage.texture.index < input.textureNames.size() && input.textureNames[usage.texture.index] != nullptr) {
            name = input.textureNames[usage.texture.index];
        }
        break;
    case ResourceKind::Buffer:
        if (usage.buffer.index < input.bufferNames.size() && input.bufferNames[usage.buffer.index] != nullptr) {
            name = input.bufferNames[usage.buffer.index];
        }
        break;
    case ResourceKind::VolumeTexture:
        if (usage.volumeTexture.index < input.volumeTextureNames.size()
            && input.volumeTextureNames[usage.volumeTexture.index] != nullptr) {
            name = input.volumeTextureNames[usage.volumeTexture.index];
        }
        break;
    }
    return name;
}
```

(Mirrors `RenderGraphCompiler.cpp`'s `ApplyUsageBarrierIfNeeded()`-style "declare a local, `switch` with
`break`, use the local after the switch" shape exactly, rather than returning directly from inside each
`case` — this sidesteps any "not all control paths return a value" compiler warning some toolchains raise
for a switch with no `default:` even when every real enumerator is covered, without weakening the
"a future enumerator must fail to compile here" property a `default: return "";` would introduce.)

This is a deliberate, named, in-scope companion fix for this phase (call it out explicitly as such in
`PHASE1_COMPLETION_REPORT.md`, not accidental scope-creep) — it lives in the exact function this phase's own
`readKinds`/`writeKinds` metadata depends on to be meaningful, and it changes ZERO rendering behavior (this
remains a pure display-string helper, never read by `RenderGraph.cpp`/`RenderGraphCompiler.cpp` themselves).

### 3.5 Tests (new tests only — do not modify any existing assertion in these three files)

- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`: **prefer extending** the already-existing
  `RenderGraphPassRecordTest.DefaultConstructedPassRecordIsEmptyAndNotCulled` test with one more
  `EXPECT_FALSE(record.isComputePass);` line rather than adding a wholly new, near-duplicate test asserting
  the same default-construction shape a second time — this still satisfies "every change to Tier 1 code
  needs a matching test change" (`AGENTS.md`) without redundant test-file bloat. A genuinely new, separate
  test is also fine if preferred; either is acceptable, just avoid two tests asserting the exact same
  default-constructed shape.
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`: new test(s) confirming (a) a plain
  `builder.AddPass("X", ...)` call's resulting `CompiledGraphInput::passes` entry has
  `isComputePass == false`; (b) a `builder.AddComputePass("Y", ...)` call's resulting entry has
  `isComputePass == true`; (c) both a `ReadTexture`/`WriteTexture` and a `ReadBuffer`/`WriteBuffer`/
  `ReadVolumeTexture`/`WriteVolumeTexture` call on the SAME pass still populate `pass.reads`/`pass.writes`
  with the correct `ResourceUsage::kind` each — this is a pure sanity check of pre-existing behavior your
  new test simply now also asserts on, not a new behavior. **Also note (cosmetic, optional):** the
  already-existing `AddComputePassBehavesIdenticallyToAddPass` test (under this same file's
  `// --- AddComputePass() - a thin, purely cosmetic alias of AddPass() ---` section header) will keep
  passing unmodified after this phase lands (it never asserts on `isComputePass` either way), but its own
  NAME and that section header both become factually stale the moment `AddComputePass()` stops being
  "purely cosmetic" — consider renaming the test (e.g. `AddComputePassStillRunsSetupAndCapturesExecuteLikeAddPass`)
  and rewording the section header at the same time you touch this file for the new tests above, purely for
  future-reader clarity; this is NOT a required assertion change (the rule in this section's own title still
  holds — no existing assertion needs to change), just a documentation-accuracy nit worth fixing while you're
  already in this file.
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`: new test(s) confirming `BuildRenderGraphSnapshot()`
  copies `isComputePass == true` through for a hand-fabricated compute `PassRecord`, `isComputePass == false`
  for a hand-fabricated graphics one, and that `readKinds`/`writeKinds` end up the correct length AND the
  correct per-entry `ResourceKind` value for a pass declaring a mix of texture/buffer/volume-texture
  reads/writes — including for a CULLED pass (confirm `isComputePass`/`readKinds`/`writeKinds` are still
  correctly populated even when `isCulled == true`, since `BuildPassSnapshot()`'s early-return-style
  `stats` guard is the ONLY thing gated on `isCulled`, not these new fields). **Also add one more new test
  here, required by 3.4b below:** a pass declaring `pass.WriteVolumeTexture(handle, ...)` (and, separately,
  one declaring `pass.ReadVolumeTexture(handle, ...)`) produces a NON-EMPTY, correct real name in
  `writeNames`/`readNames` for that entry — regression coverage for the pre-existing `ResourceUsageName()`
  gap this phase also fixes (see 3.4b); today this exact case has ZERO test coverage anywhere in this file,
  which is exactly how the gap went unnoticed since `ResourceKind::VolumeTexture` was first added.

### 3.6 What this phase deliberately does NOT do

- Does not change `RenderGraphCompiler.cpp`'s culling logic at all — culling still only ever depends on
  reachability from `finalOutputs`/`finalVolumeTextureOutputs`, never on `isComputePass`.
- Does not change `RenderGraph.cpp`'s `Execute()`/`ExecuteCompiledGraph()` at all — the existing
  `hasColorWrite`/`hasDepthWrite`-style attachment-vs-compute distinction (driven by `ResourceAccess`
  values, e.g. `IsColorAttachmentWriteAccess()`) is untouched; `isComputePass` is a separate, purely
  descriptive fact recorded alongside it, never a replacement for it.
- Does not touch `src/Editor/` at all — that is PHASE2's job. This phase's only job is making the raw fact
  available; nothing yet CONSUMES it.
- Does not touch any of the 8 real `AddComputePass()` production call sites themselves (`RenderPasses.cpp`,
  `ComputeBlurValidation.cpp`, `AtmosphereLutRenderer.cpp`) — they all already call `AddComputePass()`, so
  they automatically start reporting the new flag correctly with zero edits.

### 3.7 Fast compile check

Build just the core library + the RenderGraph test target(s) (do not run a full build/full `ctest` this
phase — see `PHASE0_MASTER_STRATEGY.md`'s own "Order of work"):

```
cmake --build build --target GreatTamanaEngineTests
```

Then run only the RenderGraph-scoped tests to confirm zero regressions before moving to PHASE2:

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R RenderGraph
```

Every pre-existing RenderGraph test must still pass unchanged; only the new tests you added this phase
should be new entries in the output.

### 3.8 Report

Write `task_manager/frame-debugger-5/PHASE1_COMPLETION_REPORT.md` documenting exactly which fields were
added, the exact diff shape at each of the 4 file touch points above (3.1/3.2/3.3/3.4, note 3.4b is a second,
distinct fix inside the SAME file 3.4 already touches, `RenderGraphSnapshot.cpp`) plus the `ResourceUsageName()`
companion fix's own before/after diff (3.4b), the full list of new test names, and explicit confirmation that
no pre-existing RenderGraph test assertion needed to change. Commit via `git_add`/`git_commit`.

**Recommendation (see `PHASE0_MASTER_STRATEGY.md`, Step 3.5): given this phase's HIGH risk level, consider
requesting a dedicated, isolated double-check of this phase specifically before the full campaign-wide
double-check reviews it alongside every other phase.**
