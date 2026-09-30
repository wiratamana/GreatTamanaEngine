# PHASE1 — `RenderTexture`/`GpuResourceFactory::CreateRenderTexture()` gain `createDepthCompanion`

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md` (this campaign's own Step 2 "Correction 3" is
the direct motivation for this phase).

## Step 1: The Goal (Where are we going?)

Give `RenderTexture` and `GpuResourceFactory::CreateRenderTexture()` one
new, trailing, DEFAULTED parameter, `bool createDepthCompanion = true`, so
a future caller (PHASE4's `RenderGraphPersistentResourceCache`, the ONLY
call site that will ever pass `false`) can opt OUT of the automatic
companion `DepthBuffer` every `RenderTexture` carries today. This is what
actually makes "color only, no depth companion" (source document Section
7) a true GPU-memory statement — without it, a `TextureDesc::hasDepth ==
false` persistent entry would still permanently carry an unused depth
allocation (roughly half the color image's own footprint) for its entire
cache lifetime, often the whole process lifetime.

When this phase is done: every pre-existing call site of
`RenderTexture`'s constructor and `GpuResourceFactory::CreateRenderTexture()`
compiles and behaves byte-for-byte unmodified (still gets a depth
companion, exactly as before) — this is a purely additive, zero-behavior-
change-for-every-existing-caller change.

## Step 2: The Situation (Where are we now?)

Confirmed by direct read, freshly, for this phase:

- `RenderTexture`'s constructor (`src/Renderer/RenderTexture.h`, ~line 86-90):
  ```cpp
  RenderTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
      int height, VkFormat format = VK_FORMAT_B8G8R8A8_UNORM,
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, const char* debugName = nullptr,
      const char* depthDebugName = nullptr, bool allowStorageImageAccess = false,
      bool allowDepthSampledAccess = false);
  ```
  `allowStorageImageAccess`/`allowDepthSampledAccess` are the exact
  precedent this phase's own new trailing parameter copies — both were
  added as trailing, defaulted, plain-`bool` parameters in earlier
  campaigns with zero call-site disruption.
- `RenderTexture::Create()` (`src/Renderer/RenderTexture.cpp`, ~line 95-193)
  unconditionally builds the companion depth buffer as its LAST step
  (~line 191): `m_depthBuffer = std::make_unique<DepthBuffer>(m_allocator,
  m_tracker, m_device, width, height, m_depthFormat, m_depthDebugName,
  m_allowDepthSampledAccess);` — no existing conditional guards this call
  at all today.
- `RenderTexture::Destroy()` (~line 195-215) already does
  `m_depthBuffer.reset();` unconditionally as its first step —
  `std::unique_ptr::reset()` on an already-null pointer is always a safe
  no-op, so NOTHING here needs to change; a `RenderTexture` constructed
  with `createDepthCompanion == false` simply has `m_depthBuffer` stay
  null for its whole lifetime, and every existing null-check
  (`Target()`'s `if (m_depthBuffer) { ... }` at ~line 86-91,
  `DepthSampler()`'s `m_depthBuffer ? m_depthBuffer->Sampler() :
  VK_NULL_HANDLE` at `RenderTexture.h` ~line 143) already handles this
  correctly with ZERO further change needed.
- `GpuResourceFactory::CreateRenderTexture()`
  (`src/Renderer/GpuResourceFactory.h` ~line 70-72):
  ```cpp
  RenderTexture CreateRenderTexture(int width, int height, VkFormat format, const char* debugName,
      const char* depthDebugName = nullptr, bool allowStorageImageAccess = false,
      bool allowDepthSampledAccess = false) const;
  ```
  implemented in `GpuResourceFactory.cpp` (locate via `search_in_dir` —
  confirm the exact forwarding line before editing) — it forwards straight
  into `RenderTexture`'s constructor; this is the ONE place that needs a
  matching new trailing parameter, forwarded straight through.
- `Renderer::CreateRenderTexture()` (`src/Renderer/Renderer.h` ~line 262)
  is a THIRD layer above `GpuResourceFactory` — confirm via
  `search_in_dir "CreateRenderTexture" Renderer.cpp` whether it also needs
  the new trailing parameter forwarded. **This phase's own required first
  step is to re-confirm, live, whether `RenderGraphResourcePool::AcquireTexture()`
  (the only render-graph-facing caller of a `RenderTexture`-creating path
  today) goes through `Renderer::CreateRenderTexture()` or directly through
  `GpuResourceFactory`** — PHASE4's cache will call whichever of these
  layers is the natural, already-established one for a render-graph-owned
  resource (mirror `RenderGraphResourcePool::AcquireTexture()`'s own exact
  call path, do not invent a new one).
- No other call site anywhere in `src/` needs to change — every one of
  them keeps getting `createDepthCompanion` at its default (`true`),
  producing an IDENTICAL depth companion to today, byte-for-byte.

## Step 3: The Plan

1. Re-confirm (via `search_in_dir`) the EXACT current signatures and
   forwarding chain of `RenderTexture`'s constructor →
   `GpuResourceFactory::CreateRenderTexture()` → (if applicable)
   `Renderer::CreateRenderTexture()`, since line numbers above are
   approximate and any earlier, unrelated work on `feature/editor-core-separation`
   may have shifted them.
2. Add `bool createDepthCompanion = true` as the new, LAST, trailing
   parameter to:
   - `RenderTexture`'s constructor (`RenderTexture.h` + `.cpp`) — store it
     as a new private member, e.g. `bool m_createDepthCompanion = true;`,
     alongside `m_allowDepthSampledAccess`.
   - `RenderTexture::Create()` — gate the existing
     `m_depthBuffer = std::make_unique<DepthBuffer>(...)` call behind
     `if (m_createDepthCompanion) { ... }`. `m_depthBuffer` simply stays
     null when `false` — every existing consumer already null-checks it
     (see Step 2).
   - `RenderTexture`'s move constructor/move-assignment operator — thread
     `m_createDepthCompanion` through `std::exchange`/plain copy exactly
     like `m_allowDepthSampledAccess` already is.
   - `GpuResourceFactory::CreateRenderTexture()` (`.h` + `.cpp`) — forward
     the new parameter straight through to `RenderTexture`'s constructor,
     unchanged otherwise.
   - `Renderer::CreateRenderTexture()`, ONLY if Step 3.1's confirmation
     shows it is genuinely in the call chain PHASE4 will use — forward
     straight through, same pattern.
3. Do NOT touch `RenderGraphResourcePool::AcquireTexture()` or any other
   pre-existing call site — every one of them keeps compiling with zero
   source change, using the new parameter's default (`true`).
4. Update each touched function's own doc comment (mirroring
   `allowStorageImageAccess`'s existing comment style) to briefly explain
   `createDepthCompanion`, referencing this campaign
   (`editor-core-separation-27`, BIG STEP 3) and pointing forward at
   `RenderGraphPersistentResourceCache` (PHASE4) as its one real consumer
   (a forward reference is fine — that class does not need to exist yet
   for this comment to be accurate/helpful).

## Step 4: Required Tests

This is Tier 2 (touches a live `RenderTexture`/`VkDevice` — no existing
Tier-1 test file constructs a real `RenderTexture` at all, confirmed by
`search_in_dir` finding no `RenderTextureTests.cpp`). Verification for
THIS phase alone (before PHASE4's cache exists to exercise it for real):

- A quick, throwaway, manual live-Editor check is sufficient here (this
  phase's own change has no new pass-author-facing behavior yet — nothing
  calls `createDepthCompanion = false` until PHASE4): run the Editor
  (`run_app_background`), confirm the Game/Scene views still render
  correctly (unaffected — they still get `createDepthCompanion` at its
  default `true`), confirm via `GET /get_logs` that no new
  warning/error appeared, then `stop_app_background`.
- The REAL, permanent regression test for `createDepthCompanion == false`
  actually skipping the depth allocation (source document's own dedicated
  acceptance checkbox: "confirmed via the GPU memory panel /
  `GpuMemoryTracker`, to carry EXACTLY ONE tracked allocation") belongs to
  PHASE4/PHASE9, once a real caller passing `false` exists — do not
  attempt to fabricate that proof in THIS phase with no real caller yet.

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full — applies unmodified.
In particular: incremental build only (`cmake --build build`), no full
`ctest`, `ask_questions` for any ambiguity (e.g. if Step 3.1's
re-confirmation reveals `Renderer::CreateRenderTexture()` is NOT actually
in PHASE4's real call chain, or reveals a FOURTH wrapping layer this phase
file did not anticipate), a `PHASE1_COMPLETION_REPORT.md`, and a git
commit covering both the code change and the report.
