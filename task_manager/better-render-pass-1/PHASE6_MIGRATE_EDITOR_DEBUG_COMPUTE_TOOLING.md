# PHASE6 — Migrate Editor Debug/Validation Compute Tooling (Migration Batch 3)

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE4` (proven migration
pattern).

---

## Step 1 — The Goal

Migrate the four remaining Editor-only compute pipelines onto reflection-based pipeline creation
+ `CommandBuffer`, with zero visual/behavioral change:

1. `src/Editor/ComputeBlurValidation.h/.cpp` (1 pipeline, `BoxBlur.comp`) — Scene-View-only,
   toggleable debug pass.
2. `src/Editor/GBufferValidation.h/.cpp` (1 pipeline, `GBufferCopy.comp`) — Scene-View-only,
   toggleable debug pass (the compute half of the MRT campaign's own validation feature — the
   graphics-side MRT write pass itself is untouched, this phase only touches the COMPUTE copy
   pipeline).
3. `src/Editor/FrameDebuggerPreviewProcessing.h/.cpp` (1 pipeline, `FrameDebuggerPreview.comp`) —
   the Frame Debugger's own Channels/Levels preview-compositing shader.
4. `src/Renderer/VolumeTexturePreviewRenderer.h/.cpp` (1 pipeline, `VolumeTexturePreview.comp`) —
   the Atmosphere volume-texture Inspector preview shader.

---

## Step 2 — The Situation (confirmed, all four already follow the identical established shape)

All four already follow the EXACT same shape as `PHASE4`/`PHASE5`'s already-migrated examples:
`DescriptorSetLayoutBuilder` + a hand-built `VkPushConstantRange` + `renderer.CreateComputePipeline(path,
layouts, range)` inside an `EnsureInitialized()`-style lazy-init method, and a
`BeginGraphPassRecording`/`Dispatch`/`EndGraphPassRecording` bracket inside the pass's `execute`
callback. Confirmed, per file, that a real push-constant block exists in all four (none of these
four omit it, unlike some of PHASE5's six Atmosphere passes):

- `ComputeBlurValidation.cpp` (~line 47-63): binding 0 = combined-image-sampler (read-only
  `Texture` input), binding 1 = storage image (`RWTexture` output); push constants = 2×
  `std::uint32_t` (width, height) — matches `PHASE1`'s own Tier-1 test fixture exactly (this file
  IS the shader `PHASE1`'s test already reflects — this phase's own live verification can directly
  compare against that already-proven reflection result).
- `GBufferValidation.cpp` (~line 99-112): binding(s) per its own documented
  `DescriptorSetLayoutBuilder` convention comment (read the file directly — this is the compute
  HALF of the MRT validation feature, reading one of the two G-buffer color outputs and copying it
  into a third, ImGui-visualizable target); push constants = 2× `std::uint32_t`.
- `FrameDebuggerPreviewProcessing.cpp` (~line 90-104): its own `struct PushConstants` (not a raw
  2-`uint32_t` pair like the other three — confirm its exact field layout by reading the file,
  since `sizeof(PushConstants)` is what the reflected size must match).
- `VolumeTexturePreviewRenderer.cpp` (~line 128-139): its own `struct PushConstants` (the
  volume-texture slicing/preview parameters — confirm its exact field layout by reading the file).

Every one of these four classes owns its own `VkDescriptorSetLayout` member and destroys it in
its own destructor — apply PHASE4's exact destructor-ownership fix (delete the now-wrong
`vkDestroyDescriptorSetLayout()` call) to all four.

---

## Step 3 — The Plan

1. Migrate `ComputeBlurValidation::EnsureInitialized()` + its real dispatch call site
   (`AddPass()`'s `execute` lambda) per `PHASE4`/`PHASE5`'s established pattern. Fix its
   destructor.
2. Migrate `GBufferValidation`'s compute pipeline (`GBufferCopy.comp`) + its dispatch call site.
   Fix its destructor. **Do not touch the separate, graphics-side MRT write pass in this same
   file** (`GBufferValidation`'s OWN "GBufferValidation" graphics pass, writing two color
   attachments — this phase is compute-pipeline-only; the graphics `Pipeline`/`CreatePipeline()`
   path is untouched by this whole campaign).
3. Migrate `FrameDebuggerPreviewProcessing`'s pipeline + dispatch call site. Fix its destructor.
4. Migrate `VolumeTexturePreviewRenderer`'s pipeline + dispatch call site. Fix its destructor.
5. For each of the four, delete any now-dead hand-restated local-size constant once confirmed
   unreferenced, and remove the now-unneeded `DescriptorSetLayoutBuilder.h` include if nothing
   else in that file still needs it.
6. Compile-check (incremental).
7. **Live visual verification**, per feature:
   - `ComputeBlurValidation`: toggle "Show Blurred Scene Output (debug)" (or whatever the real
     Scene panel toolbar checkbox is named today — confirm via `src/Editor/Panels/`) on/off, via
     HTTP if a route exists or by direct UI automation if not, and capture via `gte_send_request`
     to confirm the blur still renders identically.
   - `GBufferValidation`: toggle "Show GBuffer Validation (debug)" and confirm both G-buffer
     targets AND the compute-copied third output still look correct.
   - `FrameDebuggerPreviewProcessing`: open the Frame Debugger, enable it, capture a frame, and
     sweep its Channels/Levels controls to confirm the preview compositing still responds
     correctly (`GET /frame_debugger/...` routes, per `AGENTS.md`'s "Frame Debugger" section).
   - `VolumeTexturePreviewRenderer`: open the Atmosphere Inspector/volume-texture preview panel and
     confirm the preview slice still renders correctly.
   - Use `GET /get_logs` after each check to confirm zero new warnings/errors.
8. Write `PHASE6_COMPLETION_REPORT.md`, commit.

### Acceptance bar for this phase

- All four compute pipelines build via path-only `CreateComputePipeline()`, zero hand-built
  `DescriptorSetLayoutBuilder`/`VkPushConstantRange` remaining in any of the four files.
- All four destructors fixed (no double-destroy of a layout `ComputePipeline` now owns).
- Live, HTTP-verified: all four debug/validation features still work identically, zero new log
  warnings/errors.
