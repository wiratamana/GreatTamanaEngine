# PHASE2 — `RegisterProjectFeature()`/`UnregisterProjectFeature()` and the bounded GPU-state slot pool

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first, including Step 3.1's
Locked Decisions #4 and #9 — this phase is the heaviest in the whole
campaign and is explicitly named as a candidate for a `delegate_task`
`position: "next"` self-double-check before writing its completion report).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Step 3 (re-read it in full before starting — it is long and every paragraph
matters here).
Previous phase report to read first: `PHASE1_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

`RenderFeatureCompositor` gains two new public methods —
`RegisterProjectFeature()` and `UnregisterProjectFeature()` — that let a
caller register/unregister ONE project render feature at a time, at any
point after this object exists (unlike `OnPluginsLoaded()`'s one-time bulk
scan). Critically, a Project Assembly's own render feature's GPU state
(private target texture, blend-stage texture, their descriptor sets) is
keyed by a small, FIXED-SIZE, reusable slot index — NEVER by the human-typed
`descriptor.name` — so an unbounded number of register/rename/unregister
cycles over one long Editor session can NEVER grow this class's own GPU
resource consumption past a fixed, small, named bound
(`kMaxConcurrentProjectRenderFeatures = 16`).

By the end of this phase: a hand-constructed test can register a project
feature, see it blend correctly through the SAME `ContributeRenderGraphPasses()`
loop every plugin already uses (via PHASE1's now-reachable third branch arm),
unregister it, and confirm the freed slot gets reused by the NEXT
registration — all proven by real Tier-1 tests against a real, headless
`Core`+`RenderFeatureCompositor`, not merely reasoned about on paper. A
Project Assembly render feature must also be STRUCTURALLY distinguishable
from a `gte_plugin_abi` `_v2`/`_v3` plugin feature wherever entries are
observed by a human or an HTTP caller (Step 3.5 below) — mirroring the exact
precedent that already exists for telling `_v3` apart from `_v2`.

## Step 2: The Situation (Where are we now?)

Re-read `PHASE0_MASTER_STRATEGY.md`'s Step 2 and `PHASE1_COMPLETION_REPORT.md`
first — PHASE1 already added `Entry::projectCallback`/`projectFeatureSlot`
and the three-way branch in `ContributeRenderGraphPasses()`; this phase makes
that branch actually reachable.

The core hazard this phase exists to close (re-read the design doc's Step 3
intro in full for the complete reasoning, summarized here): a
`gte_plugin_abi` plugin's `descriptor.name` is safe to intern directly
because the set of loaded plugins is fixed for the process's whole lifetime.
A Project Assembly has NO such guarantee — BIG-STEP 2 exists specifically so
a developer can rename an effect (delete one `.cpp`, scaffold a differently-
named one) many times per session, and every hot-reload cycle re-runs
registration for whatever name is current. If GPU state were interned off
the raw name the same way plugins are, EVERY distinct name ever tried would
permanently claim its own slice of the shared, fixed, 256-set compute
descriptor pool (`GpuResourceFactory.cpp`, confirmed unchanged at
`kMaxComputeDescriptorSets = 256`, PHASE0's Step 2) — and none of it is ever
individually freed (confirmed, `ComputeDescriptorSet.h`'s own documented
contract). A developer iterating on effect names over a long session would
silently starve every OTHER compute-shaped feature in this engine (the
Atmosphere renderer, GPU-driven culling, the volume-texture preview panel)
with no error until the pool is finally exhausted somewhere unrelated.

`RenderFeatureNamePool` (`src/Core/Plugins/RenderFeatureNamePool.h`) is the
EXISTING interning mechanism `OnPluginsLoaded()`'s trailing loop already uses
— `PrivateName(pluginName, viewName)` -> `"<pluginName>_<viewName>_Private"`,
`AccumName()` -> `"..._Accum"`, `BlendPassName()` -> `"..._Blend"`, plus
per-view-only `SeedName()`/`SeedCopyPassName()`. This phase does NOT change
`RenderFeatureNamePool` itself — it changes WHAT STRING is fed into
`PrivateName()`/`AccumName()`/`BlendPassName()` for a `projectCallback` entry
specifically: a synthetic, slot-derived key
(`"ProjectFeatureSlot" + std::to_string(entry.projectFeatureSlot)`), never
`entry.descriptor.name`.

**Confirmed, fresh, re-reading `src/Core/Plugins/RenderFeatureDebugEntry.h`/
`src/Renderer/RenderGraph/RenderGraphMetadata.cpp`/
`src/Editor/Panels/RenderGraphPanel.cpp` while writing this phase file**: a
`RenderFeatureDebugEntry` (the struct `RenderFeatureCompositor::DebugSnapshot()`
returns, consumed by BOTH the Editor's "Render Graph" panel AND
`GET /render_graph`'s JSON `render_features[]` array, key `"render_features"`,
`RenderGraphMetadata.cpp` line 384) has exactly ONE ABI-distinguishing field
today, `bool isV3` (JSON key `"is_v3"`, `RenderGraphMetadata.cpp` line 422),
added specifically because "the ONLY way to tell which ABI a
`render_features[]` row belongs to is by eyeballing the plugin's own chosen
`name` string" was a real, previously-confirmed gap (see that field's own
doc comment, `RenderFeatureDebugEntry.h`). `RenderGraphPanel.cpp` (line 460)
renders a `"[v3]"` colored tag next to any entry with `isV3 == true`. Once
this phase adds a THIRD module-kind, the EXACT SAME gap reappears one level
up: a `projectCallback` entry has `isV3 == false` (since `moduleV3` is
`nullptr` for it), making it structurally IDENTICAL, at the observation
layer, to a real `_v2` plugin entry — Step 3.5 below closes this the same
way `isV3` itself was closed.

## Step 3: The Plan (detailed strategy)

### 3.1 — The slot constant and free-list, `RenderFeatureCompositor.h`

Add a new `private` constant, alongside this class's other private members:

```cpp
// Generously sized against realistic usage (mirrors GpuResourceFactory's
// own "generously sized, a low hundreds not thousands" compute-pool sizing
// philosophy) - 16 concurrently REGISTERED Project Assembly render features
// (summed across every currently-loaded project) is far beyond what any
// real session needs at once, while costing at most 16 (slots) x 2 (views)
// x 2 (private + blend descriptor sets per slot) = 64 permanent entries out
// of the shared pool's fixed 256 - comfortable headroom alongside every
// other existing compute consumer (confirmed against
// GpuResourceFactory.cpp's real, current pool sizing before this phase was
// called done - re-confirm again yourself, live, before writing this
// phase's own completion report).
static constexpr int kMaxConcurrentProjectRenderFeatures = 16;
```

Add a new `private` member: `std::vector<int> m_freeProjectFeatureSlots;`.
Initialize it in the constructor body (`RenderFeatureCompositor.cpp`'s
constructor, currently just an initializer list with an empty body) to
`{0, 1, ..., kMaxConcurrentProjectRenderFeatures - 1}` — e.g.
`for (int i = 0; i < kMaxConcurrentProjectRenderFeatures; ++i) { m_freeProjectFeatureSlots.push_back(i); }`.

### 3.2 — New public methods

```cpp
// RenderFeatureCompositor.h - new public methods, placed immediately after
// EnsureV3OpDescriptorSet()'s own declaration.

// Callable incrementally, any time after this object exists (unlike
// OnPluginsLoaded()'s one-time bulk scan) - a Project Assembly registers its
// own render feature(s) from its own GTE_RegisterProject entry point.
// `descriptor` must already be fully built (via MakeRenderFeatureDescriptor(),
// plugins/gte_plugin_abi/RenderFeatureDescriptor.h) before this is called -
// this method never touches descriptor.name's own length/validity itself
// (Core::RegisterProjectRenderFeature(), PHASE3, does that BEFORE calling
// this). Returns false (GTE_LOG_WARNING, never crashes) if descriptor.name
// already exists in either m_postComposite or m_preUi (mirrors
// FindEntryByName()'s own linear-scan convention), OR if every one of the
// kMaxConcurrentProjectRenderFeatures slots is currently claimed by some
// other still-registered project render feature.
bool RegisterProjectFeature(const GtePluginRenderFeatureDescriptor& descriptor,
    ProjectRenderFeatureCallback callback);

// Removes a previously-registered project feature by name, releasing its
// claimed GPU-state slot back to the free list so a LATER registration (this
// project's, a renamed replacement, or a different project's) can reuse it.
// Returns false if no entry with that name exists, or if the found entry is
// NOT a project feature (its `projectCallback` is unset) - a harmless,
// logged no-op either way; never touches a moduleV2/moduleV3 entry's slot
// bookkeeping (it never held one).
bool UnregisterProjectFeature(const char* name);
```

### 3.3 — Implementation, in order (`RenderFeatureCompositor.cpp`)

`RegisterProjectFeature()`:
1. Check `m_freeProjectFeatureSlots.empty()` — if true, `GTE_LOG_WARNING`
   naming `kMaxConcurrentProjectRenderFeatures` and `descriptor.name`, return
   `false` immediately. (Order relative to the duplicate-name check below is
   not load-bearing — either order is fine, per the design doc — but never
   partially claim a slot before confirming the name is not a duplicate.)
2. Call `FindEntryByName(descriptor.name)` — if non-null, `GTE_LOG_WARNING`
   naming the duplicate, return `false` (do not consume a slot).
3. Pop a slot index off `m_freeProjectFeatureSlots` (e.g. `.back()` then
   `.pop_back()` — order within the free list is irrelevant).
4. Construct an `Entry`: `entry.projectCallback = std::move(callback);
   entry.descriptor = descriptor; entry.projectFeatureSlot = <claimed slot>;`
   (`moduleV2`/`moduleV3` stay `nullptr`, `enabledOverride` stays its default
   `true`).
5. Route into `m_postComposite`/`m_preUi` using the SAME rule
   `OnPluginsLoaded()` already uses:
   `descriptor.stage == RenderFeatureStage::PreUI ? m_preUi : m_postComposite`
   (an unwired stage — `PreOpaque`/`PostOpaque`/`PostTransparent` — should
   ALSO be refused here exactly like `OnPluginsLoaded()` refuses it: log the
   same "not wired in this engine build" warning, release the slot back
   before returning `false`, never push an Entry for it. Re-read
   `OnPluginsLoaded()`'s own refusal block, lines 311-319, and mirror it —
   this is the one piece of `OnPluginsLoaded()`'s own validation logic this
   new incremental path must NOT skip).
6. Push the new `Entry`, then call `SortAndDetectCollisionsInStage()` on
   whichever vector just grew (reuse, never duplicate, that existing static
   method).
7. Re-seed the name pool for the newly-added entry, for BOTH `"Game"` and
   `"Scene"` views — mirroring `OnPluginsLoaded()`'s own trailing loop, with
   the ONE deliberate difference: the string fed into `PrivateName()`/
   `AccumName()`/`BlendPassName()` is `"ProjectFeatureSlot" +
   std::to_string(entry.projectFeatureSlot)`, never `descriptor.name`. Write
   a small local helper or inline this — e.g.:
   ```cpp
   const std::string gpuStateKey = "ProjectFeatureSlot" + std::to_string(claimedSlot);
   static constexpr const char* kViewNames[] = { "Game", "Scene" };
   for (const char* viewName : kViewNames) {
       m_namePool.PrivateName(gpuStateKey, viewName);
       m_namePool.AccumName(gpuStateKey, viewName);
       m_namePool.BlendPassName(gpuStateKey, viewName);
   }
   ```
8. Return `true`.

`UnregisterProjectFeature()`:
1. Call `FindEntryByName(name)` — if null, return `false` (no-op, no log
   needed beyond what `FindEntryByName()`'s own caller convention already
   does elsewhere — mirror `SetFeatureEnabled()`'s existing "return false, no
   crash" contract for an unknown name).
2. If found but `entry->projectCallback` is NOT set (i.e. this is a
   `moduleV2`/`moduleV3` entry), `GTE_LOG_WARNING` naming it and return
   `false` — refuse to touch it through this path.
3. Push `entry->projectFeatureSlot` back onto `m_freeProjectFeatureSlots`.
4. Erase the entry from whichever vector (`m_postComposite`/`m_preUi`)
   actually holds it (re-derive which one the same defensive way
   `SetFeaturePriority()` already does — never assume from
   `entry->descriptor.stage` alone).
5. Deliberately do NOT touch `m_privateTargetStates`/`m_blendStageStates`
   entries for this slot's own interned names — they are left exactly as
   they are (not destroyed, not resized to zero); the NEXT occupant of this
   slot resizes/`Rewrite()`s them in place, the same way `EnsureTextureSized()`/
   `Rewrite()` already handle any entry's own extent changing frame-to-frame
   today. This is precisely what keeps total GPU resource count bounded by
   `kMaxConcurrentProjectRenderFeatures`, never by lifetime name-try count.
6. Return `true`.

### 3.4 — `ContributeRenderGraphPasses()`'s `gpuStateKey` rewrite

This is the ONE other change this phase makes to that method's existing
per-entry loop (`RenderFeatureCompositor.cpp`, the loop PHASE1 already gave a
three-way branch). Today, `privateName`/`accumName`/`blendPassName` are all
computed from `pluginName` (`entry.descriptor.name`) directly. Change this to
compute one `gpuStateKey` string per entry FIRST, then feed THAT into every
`m_namePool.*Name()` call for that entry:

```cpp
const std::string pluginName = entry.descriptor.name; // unchanged - still used for FindEntryByName()/logs/adapter construction.
const std::string gpuStateKey = (entry.projectCallback)
    ? ("ProjectFeatureSlot" + std::to_string(entry.projectFeatureSlot))
    : pluginName;

const char* privateName = m_namePool.PrivateName(gpuStateKey, viewName);
// ... AccumName(gpuStateKey, viewName), BlendPassName(gpuStateKey, viewName) - same substitution, every call site in this loop.
```

Everything else in the loop (adapter construction using `pluginName`,
`resolved->target`/`privateTarget` plumbing, `DispatchBlend()` calls) is
UNCHANGED — `pluginName` (the real, human-typed name) is still what is
logged/displayed/used for adapter construction; ONLY the GPU-resource
interning key changes. Confirm, by direct read after this edit, that a
`moduleV2`/`moduleV3` entry's `gpuStateKey` is IDENTICAL to its
`pluginName` (i.e. this change is a complete no-op for every existing plugin
entry) — this is the single most important regression check in this phase.

As part of this same phase's diff, also update `src/Core/Plugins/RenderFeatureNamePool.h`'s
own top-of-file doc comment: it currently states "The set of loaded _v2
plugins AND the set of known views... are both fixed for the process's
entire remaining lifetime... hot reload is a permanent non-goal... every
name below is looked up once, lazily... never re-interned per frame." This
becomes misleading once this phase lands — a `projectCallback` entry's own
`gpuStateKey` ("ProjectFeatureSlot0".."ProjectFeatureSlot15") IS still a
fixed, bounded universe of possible keys for the process's entire lifetime
(so the class's own actual behavior/guarantee is unchanged and still
correct), but the surrounding prose's blanket claim that hot reload is
"a permanent non-goal" for this pool is no longer true for this new
consumer. Adjust that comment to state the real, current invariant precisely
(a bounded, fixed-size KEY SPACE is what makes lazy, never-re-interned
lookup safe — not "nothing here is ever reloaded") rather than leaving a
stale claim that contradicts this same phase's own new code three files
away.

### 3.5 — Debug/HTTP observability: `RenderFeatureDebugEntry` gains `isProjectFeature`

Mirroring `isV3`'s own exact precedent (Step 2 above), add a new field to
`RenderFeatureDebugEntry` (`src/Core/Plugins/RenderFeatureDebugEntry.h`):

```cpp
// A Project Assembly's own on-screen render feature
// (Core::RegisterProjectRenderFeature(), editor-core-separation-23 campaign)
// - mutually exclusive with isV3 (a moduleV2/moduleV3 entry never sets this;
// a projectCallback entry never sets isV3). Added for the identical reason
// isV3 itself was added: without a structural field, a Project Assembly
// feature and a real _v2 plugin feature are indistinguishable at this
// observation layer (both show isV3 == false).
bool isProjectFeature = false;
```

Populate it in `RenderFeatureCompositor::DebugSnapshot()`'s own `appendStage`
lambda, alongside the existing `debugEntry.isV3 = (entry.moduleV3 != nullptr);`
line:

```cpp
debugEntry.isProjectFeature = static_cast<bool>(entry.projectCallback);
```

Update the two real consumers so this new field is not silently invisible:
  - `src/Renderer/RenderGraph/RenderGraphMetadata.cpp` (line ~422, the
    `{ "is_v3", entry.isV3 }` JSON field) — add a sibling
    `{ "is_project_feature", entry.isProjectFeature }`, same snake_case
    convention, same object.
  - `src/Editor/Panels/RenderGraphPanel.cpp` (line ~460, the
    `if (entry.isV3) { ... "[v3]" ... }` block) — add a sibling
    `if (entry.isProjectFeature) { ... }` rendering a distinctly-colored
    `"[Project]"` tag, mirroring the existing block's exact shape
    (`ImGui::SameLine()` + `ImGui::TextColored(...)`), so a human looking at
    the "Render Graph" panel can tell a Project Assembly feature apart from a
    plugin feature at a glance, exactly like `"[v3]"` already lets them tell
    `_v3` apart from `_v2`.

This is a genuinely new, small, additive field on an EXISTING debug-only
struct/JSON body/UI row — not a new HTTP endpoint, not a new query
parameter, not a behavior change to anything `_v2`/`_v3` plugins already do.
Cover it with a Tier-1 test in 3.6 below (a registered project feature's
`DebugSnapshot()` entry has `isProjectFeature == true` and `isV3 == false`),
and confirm it live in PHASE6's own screenshot/JSON verification.

### 3.6 — Tier-1 tests

New file: `tests/Core/Plugins/RenderFeatureCompositorProjectFeatureTests.cpp`,
using the EXACT `HeadlessSurfaceProvider` + `NoopHostServices` + real `Core`
fixture pattern from `ProjectAssemblyRegistrationLedgerTests.cpp` (PHASE0's
Step 2 citation) — `GTEST_SKIP()` identically if this machine's Vulkan
driver/loader doesn't support `VK_EXT_headless_surface`. Access the real
compositor via `core->GetRenderFeatureCompositor()` (confirmed non-null on
any real `Core`, PHASE0 Step 2). Use `MakeRenderFeatureDescriptor()` to build
every test descriptor (never a hand-rolled aggregate initializer). This new
test file must ALSO be added to `tests/CMakeLists.txt`'s own test-file list —
that list is hand-maintained, NOT a glob (confirmed: every sibling
`tests/Core/Plugins/*.cpp` file, e.g. `ProjectAssemblyRegistrationLedgerTests.cpp`,
is individually listed there with its own short comment) — add one new line
for this file, placed alongside its `Core/Plugins/*.cpp` siblings, mirroring
their exact comment style. Skipping this step means the new tests silently
never compile or run at all; the targeted
`ctest -C Debug --output-on-failure -R RenderFeatureCompositorProjectFeature`
this phase's own End-of-phase step requires would then report "No tests were
found" rather than a real pass — do not mistake that for success. Cover, at
minimum:
  1. `RegisterProjectFeature()` succeeds for a fresh, unique name; the
     returned/observable slot (via `DebugSnapshot()` — extend
     `RenderFeatureDebugEntry`/`DebugSnapshot()` ONLY if genuinely needed to
     observe the slot for this test; prefer observing behavior instead if
     possible, and `ask_questions` before widening a debug-only struct's
     public shape for a test's convenience BEYOND the `isProjectFeature`
     field 3.5 already adds for a different, necessary reason) is claimed.
  2. Registering a DUPLICATE name fails (`false`), and does not consume an
     extra slot (confirmed by registering `kMaxConcurrentProjectRenderFeatures`
     OTHER distinct names afterward and observing all of them still succeed).
  3. `UnregisterProjectFeature()` on an existing name succeeds; the freed slot
     is reused by the NEXT `RegisterProjectFeature()` call with a DIFFERENT
     name (confirm via a targeted, deterministic test: exhaust every slot
     with distinctly-named registrations, unregister exactly ONE specific
     name, then register one more distinct name and confirm it succeeds
     where it would otherwise have failed — this indirectly proves reuse
     without needing to inspect the raw slot index at all).
  4. Registering `kMaxConcurrentProjectRenderFeatures + 1` distinctly-named
     features with NONE ever unregistered: the first
     `kMaxConcurrentProjectRenderFeatures` succeed, the LAST one fails
     (`false`), and all previous ones remain registered (confirm via
     `DebugSnapshot()` still listing every earlier name).
  5. `UnregisterProjectFeature()` on an unknown name fails harmlessly
     (`false`, no crash).
  6. `UnregisterProjectFeature()` called with the name of an EXISTING
     `moduleV2`/`moduleV3` entry fails and does not disturb the slot free
     list — this specific scenario needs a REAL loaded plugin, which this
     fixture cannot easily provide; if constructing one is impractical here,
     it is acceptable to instead unit-test this refusal path by asserting
     `FindEntryByName()`'s returned entry's `projectCallback` is checked
     BEFORE any slot mutation happens (a code-level confirmation via a
     smaller, targeted test or a careful manual trace) — `ask_questions` if
     genuinely unsure which approach to take.
  7. An unwired stage (`PreOpaque`) passed to `RegisterProjectFeature()` is
     refused (`false`), releasing its claimed slot back (confirmed by then
     registering `kMaxConcurrentProjectRenderFeatures` OTHER distinct names
     and observing all of them succeed — proving the refused attempt did not
     leak a slot).
  8. A registered project feature's callback is ACTUALLY INVOKED once a real
     frame's `ContributeRenderGraphPasses()` runs (this may require a
     lightweight, real render-graph-frame harness — check whether one already
     exists for testing `RenderPipeline`/render-graph passes elsewhere in
     `tests/Renderer/RenderGraph/`; reuse it if so, otherwise this specific
     assertion may be deferred to PHASE6's own LIVE verification instead —
     `ask_questions` if this is unclear once you look at what test
     infrastructure genuinely exists).
  9. A registered project feature's `DebugSnapshot()` entry has
     `isProjectFeature == true` AND `isV3 == false` (3.5 above) — the single
     most important assertion for that new field's own correctness.

### 3.7 — Ambiguity checkpoints

  - If `EnsureBuiltinsRegistered()`/`m_device` initialization (see
    `ContributeRenderGraphPasses()`'s own existing comment about a real,
    previously-fixed bug where `m_device` stayed `VK_NULL_HANDLE`) interacts
    with a headless test fixture in any surprising way, `ask_questions`
    before working around it silently.
  - If test item 3.6.6 above genuinely cannot be exercised without a live
    plugin `.dll`, `ask_questions` about whether a lighter-weight structural
    test is acceptable for this phase, with full live coverage deferred to
    PHASE6.
  - This phase is large. Per PHASE0's Locked Decision #9, you may
    `delegate_task` with `position: "next"` ONCE, after finishing the
    implementation + tests above, specifically to re-verify: (a) the
    `gpuStateKey` substitution is a complete no-op for `moduleV2`/`moduleV3`
    entries, (b) the slot free-list never leaks on any refusal path, (c) all
    Tier-1 tests in 3.6 above genuinely pass. That sub-task must NOT create
    its own report file — it reports back to you inline, and you write the
    ONE `PHASE2_COMPLETION_REPORT.md` yourself afterward.

### 3.8 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. `ctest -C Debug --output-on-failure -R RenderFeatureCompositorProjectFeature`
   passes (targeted filter, not the full suite).
3. Write `PHASE2_COMPLETION_REPORT.md`: the exact slot-pool design as
   implemented, the `gpuStateKey` diff, the `isProjectFeature` diff across
   `RenderFeatureDebugEntry.h`/`RenderGraphMetadata.cpp`/`RenderGraphPanel.cpp`,
   every Tier-1 test's outcome, and explicit confirmation that `moduleV2`/
   `moduleV3` behavior is unchanged.
4. `git_add` + `git_commit` covering the header/`.cpp` changes, the new test
   file, and the report.
