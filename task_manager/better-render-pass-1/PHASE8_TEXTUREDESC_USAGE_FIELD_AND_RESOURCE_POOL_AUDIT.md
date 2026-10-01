# PHASE8 — `TextureDesc::usage` Bitmask Field + RenderGraphResourcePool Audit

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Independent of `PHASE4`-`PHASE7`
(touches `RenderGraphTypes.h`/`RenderGraphResourcePool.h/.cpp` only) — may run any time after
`PHASE3`; scheduled here purely to keep the compute-pipeline migration batches uninterrupted.

---

## Step 1 — The Goal

Give `rg::TextureDesc` a genuine, equality-compared `usage` bitmask field (R5), matching the
client's own rough sketch (`TextureUsage::Storage | TextureUsage::TransferSrc`), audit
`RenderGraphResourcePool`'s pooling logic so two textures differing only in `usage` are never
silently pooled together, and — as the direct, concrete payoff — make a genuinely TRANSIENT/
pooled `RWTexture` possible for the first time (closing a gap
`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`'s Section A.6/C has documented as open
since the original compute-shader campaign).

---

## Step 2 — The Situation

### 2.1 `TextureDesc` today (`RenderGraphTypes.h`, confirmed ~line 278-291)

```cpp
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool hasDepth = false;
    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};
```

This file's own "standing rule" comment (Step 2.5, `PHASE0`) is explicit: a field only belongs
here if it changes whether two requests can share one physical allocation — `usage` genuinely
qualifies (a storage-capable image and a non-storage image are NOT Vulkan-interchangeable), unlike
the old `debugName` field this same file's own git history already removed for the opposite
reason (comparing pointer identity, not content, silently defeating pooling).

### 2.2 `RenderGraphResourcePool::AcquireTexture()` today (confirmed, `RenderGraphResourcePool.h`
~line 118, `.cpp` implementation) — matches `desc == entry.desc` purely by `TextureDesc`'s own
`operator==`. Once `usage` is added as a real, compared field, this matching ALREADY becomes
correct automatically (the default `operator==` compares every member) — **the real risk this
phase must audit is not the comparison itself, but whether `AcquireTexture()`'s OWN call into
`Renderer::CreateRenderTexture()` actually THREADS `desc.usage` through into that call's
`allowStorageImageAccess`/`allowDepthSampledAccess` parameters** (confirmed, `GpuResourceFactory.h`
~line 79-81: `CreateRenderTexture(int width, int height, VkFormat format, const char* debugName,
const char* depthDebugName = nullptr, bool allowStorageImageAccess = false, bool
allowDepthSampledAccess = false, bool createDepthCompanion = true)`) — today,
`RenderGraphResourcePool::AcquireTexture()` (per its own `.cpp`) calls this with
`allowStorageImageAccess` hardcoded to its default (`false`) always, since `TextureDesc` has no
such field to read from at all yet. This is the ACTUAL, concrete bug this phase's `usage` field
must fix, not merely a cosmetic addition.

### 2.3 `TextureUsage` bitmask — design

Mirror `ResourceAccess`'s own "named after WHAT THEY DO" convention (`RenderGraphTypes.h`'s own
comment on `ResourceAccess`). A `VkFlags`-shaped bitmask enum:

```cpp
enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1u << 0,     // default-equivalent, real-world today's implicit meaning: a plain sampled/color-attachment-capable texture
    Storage = 1u << 1,     // VK_IMAGE_USAGE_STORAGE_BIT - an RWTexture, mirrors allowStorageImageAccess
    TransferSrc = 1u << 2, // VK_IMAGE_USAGE_TRANSFER_SRC_BIT - a blit/copy source (AddBlitPass() src)
    TransferDst = 1u << 3, // VK_IMAGE_USAGE_TRANSFER_DST_BIT - a blit/copy destination (AddBlitPass() dst)
};
constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept { /* ... */ }
constexpr bool HasFlag(TextureUsage value, TextureUsage flag) noexcept { /* ... */ }
```

Confirm, by reading `RenderTexture.h`/`GpuResourceFactory.cpp`'s real `CreateRenderTexture()`
implementation, EXACTLY which combination of `VkImageUsageFlags` bits a `RenderTexture` is built
with UNCONDITIONALLY today (expect at minimum color-attachment + sampled, always — these are
implicit/always-on and do not need their own `TextureUsage` bit at all; only the OPT-IN
possibilities — storage, and whatever transfer-src/dst opt-ins already exist as separate
mechanisms today, if any — need a bit here). Do not invent bits for usage combinations this engine
has no real mechanism to request yet (e.g. do not add a `TextureUsage::InputAttachment` bit with
no real consumer — mirrors this whole codebase's "never speculative, only once a real, concrete
need appears" discipline, see `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`'s own
framing).

`operator==` for `TextureDesc` stays `= default` — appending `TextureUsage usage =
TextureUsage::None;` as a new field is automatically included in the default comparison with zero
extra code, exactly like `hasDepth` already is.

---

## Step 3 — The Plan

1. Add the `TextureUsage` enum + `operator|`/`HasFlag` (or a small, equivalent helper set) to
   `RenderGraphTypes.h`, placed near `ResourceAccess` (same file, same general "small vocabulary
   enum" section).
2. Add `TextureUsage usage = TextureUsage::None;` to `TextureDesc`, at the END of the struct
   (never inserted in the middle — mirrors this codebase's universal struct-field-append rule).
   Add a matching doc comment explaining why this field genuinely belongs in the
   equality-compared struct (cite the file's own "standing rule" comment directly, per `PHASE0`
   Step 2.5).
3. Update `RenderGraphBuilder::CreateTexture(name, desc)` — no signature change needed (the
   caller already passes a whole `TextureDesc`, which now simply carries more information); no
   code change needed in `RenderGraphBuilder.cpp` either, since `CreateTexture()` is a thin
   pass-through storing the desc into a `TextureSlot` — confirm this by reading it, and confirm no
   change is genuinely needed there (if one is, this step's own assumption was wrong — stop and
   re-read `RenderGraphBuilder::CreateTexture()`'s real body before proceeding).
4. **Thread `desc.usage` into `RenderGraphResourcePool::AcquireTexture()`'s real
   `Renderer::CreateRenderTexture()` call** — this is the load-bearing fix (Step 2.2): translate
   `HasFlag(desc.usage, TextureUsage::Storage)` into that call's `allowStorageImageAccess`
   argument. Decide (and document) whether `TransferSrc`/`TransferDst` map onto any existing
   `CreateRenderTexture()` parameter today, or whether those two bits are being added now purely
   so a FUTURE phase/campaign has the vocabulary ready. **CONFIRMED by direct reading of
   `RenderTexture.cpp::Create()` — they are NOT already included.** That method's own
   `imageInfo.usage` is unconditionally exactly `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
   VK_IMAGE_USAGE_SAMPLED_BIT`, ORing in `VK_IMAGE_USAGE_STORAGE_BIT` only when
   `m_allowStorageImageAccess` is true — `TRANSFER_SRC_BIT`/`TRANSFER_DST_BIT` are never included,
   under any condition, today. **Also confirmed: this engine's one real `AddBlitPass()` consumer,
   `src/Editor/BlitValidation.cpp`, always blits between its own directly-owned `RenderTexture`s
   (`renderer.CreateRenderTexture()`, then `builder.ImportTexture()`) — never a pooled,
   `TextureDesc`-driven `builder.CreateTexture()` resource.** So `TextureUsage::TransferSrc`/
   `TransferDst` have ZERO real, wired consumer today: adding them to the enum is genuinely,
   currently pure vocabulary with no effect on any actual `VkImageUsageFlags` value anywhere,
   which is in tension with this same file's own "never speculative, only once a real, concrete
   need appears" discipline (Step 2.3 above). Resolve this by adding the two bits anyway (matching
   `PHASE0`'s own explicit Step 1 deliverable, which names all four), but WITHOUT inventing any new
   `CreateRenderTexture()` plumbing for them in this phase — leave `RenderGraphResourcePool::
   AcquireTexture()` threading ONLY `TextureUsage::Storage` through (Step 4 below), and add an
   explicit, loud doc comment directly on the `TransferSrc`/`TransferDst` enumerators stating
   plainly: "NOT YET WIRED into `RenderTexture::Create()`'s own image-usage flags — no
   `CreateRenderTexture()` parameter accepts them yet; a future pass needing to blit into a
   pooled/`CreateTexture()`-declared resource (today's one real blit consumer,
   `BlitValidation.cpp`, always uses a directly-owned, imported `RenderTexture` instead) would need
   to add that plumbing then, mirroring how `Storage`/`allowStorageImageAccess` already works." This
   keeps the enum forward-compatible and honest rather than silently implying a capability that does
   not exist yet.
5. Add/extend Tier-1 tests in `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`: a new test
   proving two `TextureDesc` values differing ONLY in `usage` compare UNEQUAL (the core
   pooling-safety property this whole phase exists to guarantee). **Correction, confirmed by direct
   reading**: this file's existing `RenderGraphDescTest` suite has NO default-constructed
   `TextureDesc{}` test today at all (there is no test named anything like
   "DefaultConstructed...TextureDesc..." — every existing case in that suite constructs a
   fully-specified, non-default `TextureDesc{ 1920, 1080, ... }`) — so there is nothing existing to
   "extend" here; ADD a brand-new test asserting `TextureDesc{}.usage == TextureUsage::None` (and,
   while adding it, this is also a reasonable place to assert the OTHER default field values too,
   e.g. `width == 0`/`height == 0`/`format == VK_FORMAT_UNDEFINED`/`hasDepth == false`, since no test
   currently covers a plain default-constructed `TextureDesc` at all).
6. Add a Tier-1 (or, if a live `VkDevice` is genuinely unavoidable, a clearly-labeled Tier-2
   manual check) test/verification that `RenderGraphResourcePool::AcquireTexture()` actually
   produces a storage-capable `RenderTexture` when `desc.usage` includes `TextureUsage::Storage`,
   and does NOT when it doesn't — if `RenderGraphResourcePoolTests.cpp` (or similar) already
   exists and already exercises `AcquireTexture()` against a real/fake `Renderer`, extend it there
   following its own existing fixture pattern; if no such test file exists yet (this class lives
   in the Tier-2, GPU-dependent bucket per `AGENTS.md`), document this as a manual verification
   step instead and perform it live (construct a transient `RWTexture` via
   `builder.CreateTexture(name, TextureDesc{ w, h, format, false, TextureUsage::Storage })`
   from a real, throwaway test pass, dispatch a trivial compute write into it, and confirm via
   `gte_send_request`/a debugger that it renders/writes correctly — revert the throwaway pass
   before finishing, this is a one-time confidence check, not a permanent addition).
7. Compile-check (incremental), run the new/changed Tier-1 tests.
8. Write `PHASE8_COMPLETION_REPORT.md` (state clearly which real gap this phase closed —
   "a render-graph-declared `CreateTexture()` resource can now genuinely opt into
   `VK_IMAGE_USAGE_STORAGE_BIT` for the first time" — and cross-reference
   `COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`'s Section A.6/C.2 as the
   long-standing gap this closes, for anyone reading that older document later), commit.

### Acceptance bar for this phase

- `TextureDesc::usage` exists, is a genuinely equality-compared field, and two descs differing
  only in `usage` are proven (by a passing Tier-1 test) to compare unequal.
- `RenderGraphResourcePool::AcquireTexture()` genuinely honors `TextureUsage::Storage`, proven
  either by an automated test or an explicit, documented manual/live verification.
- Zero change to any pre-existing `CreateTexture()` call site's required syntax (every existing
  call simply keeps constructing a `TextureDesc` with `usage` left at its default,
  `TextureUsage::None`, and behaves byte-for-byte as before).
