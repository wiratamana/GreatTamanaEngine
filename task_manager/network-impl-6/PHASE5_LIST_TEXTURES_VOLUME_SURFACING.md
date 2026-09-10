# PHASE5_LIST_TEXTURES_VOLUME_SURFACING

Parent: `PHASE0_MASTER_STRATEGY.md` — **READ THAT FILE FIRST.**
Previous: `PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md` — read its
own `PHASE4_COMPLETION_REPORT.md` before starting.

## Step 1 — The Goal

Make `GET /list_textures` also list every currently-registered VOLUME
texture, so an LLM/AI agent can **discover** which `texture_name` values are
worth calling `GET /get_texture` with, without having to already know the
Atmosphere feature's internal naming convention. Each entry gains a `kind`
field (`"texture2d"` or `"texture3d"`) so a caller can tell the two apart,
and a volume entry additionally reports its full 3D extent (width/height/
depth) instead of just width/height. By the end of this phase, `GET
/list_textures` returns both kinds side by side in one flat array, and
every EXISTING 2D entry's JSON shape gains exactly one new field (`kind`)
and nothing else changes about it.

## Step 2 — The Situation / The Problem

Re-read (in this order) before writing anything:

1. `src/Application/FrameCaptureBridge.h`'s `PublishedTextureListEntry`
   struct and `PublishTextureList()`/`GetPublishedTextureList()` methods.
2. `src/Network/NetworkRoutes.h`'s `TextureListEntryView` struct and
   `BuildListTexturesResponseJson()`.
3. `Application.cpp`'s existing `GET /list_textures` data-publishing block
   (right after the `NamedTexture` capture block Phase 4 modified — the one
   that builds `std::vector<Network::TextureListEntryView>`-shaped data
   from `m_renderGraph.ListDebugTextures()` — actually check exactly which
   struct it builds first; per `FrameCaptureBridge.h`'s own doc comment
   this is `PublishedTextureListEntry`, published via
   `m_captureBridge.PublishTextureList(...)`, and it is `NetworkServer.cpp`
   that later copies `PublishedTextureListEntry` -> `TextureListEntryView`
   field-by-field, per that struct's own "two nearly-identical, DELIBERATELY
   SEPARATE structs" rule — see `AGENTS.md`, "Named Texture Capture").
4. `NetworkServer.cpp`'s own `GET /list_textures` route handler — the exact
   spot that calls `m_captureBridge->GetPublishedTextureList()` and copies
   each `PublishedTextureListEntry` into a fresh `TextureListEntryView`.

This phase must preserve that EXACT three-struct-hop shape (`Application.cpp`
resolves raw engine state -> `PublishedTextureListEntry` ->
`NetworkServer.cpp` copies -> `TextureListEntryView` -> `NetworkRoutes.h`
builds JSON) — do NOT collapse it into fewer hops "for convenience"; this is
a deliberate, load-bearing layering rule this codebase already documents
(`AGENTS.md`: "never collapse them into one shared type crossing the
`Application`/`Network` layer boundary").

## Step 3 — The Plan

### 3.1 — `FrameCaptureBridge.h`: extend `PublishedTextureListEntry`

Add two fields:

```cpp
struct PublishedTextureListEntry {
    std::string name;
    std::string regime;
    std::string format;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasDepth = false;
    std::uint64_t framesSinceUpdate = 0;

    // network-impl-6 campaign, Phase 5. "texture2d" (the only kind that
    // existed before this campaign) or "texture3d". Every OLD call site
    // building a 2D entry is updated to set this explicitly to "texture2d"
    // (never left to an implicit/defaulted value, so it's obvious at each
    // call site which kind is being built) - see Application.cpp.
    std::string kind = "texture2d";

    // Meaningful ONLY when kind == "texture3d" (the volume's Z/depth texel
    // count) - always 0 for a "texture2d" entry. NOT to be confused with
    // hasDepth above (a 2D texture's OWN depth-BUFFER availability) - a
    // volume entry always has hasDepth == false (see VolumeTarget.h: a
    // volume texture has no depth-companion concept at all), that field
    // is untouched/reused as-is for this new kind, just always false.
    std::uint32_t depth = 0;
};
```

No change needed to `PublishTextureList()`/`GetPublishedTextureList()`
themselves — both already move/copy the whole struct by value.

### 3.2 — `Application.cpp`: publish volume entries too

Find the existing block building `std::vector<PublishedTextureListEntry>`
from `m_renderGraph.ListDebugTextures()` (right after Phase 4's new
branch). Two changes:

1. Every entry built from a `rg::DebugTextureSnapshot` (the pre-existing
   loop) now explicitly sets `entry.kind = "texture2d";` (was implicitly
   the default before this phase; make it explicit at the call site per
   3.1's own comment).
2. Immediately after that existing loop, add a second loop over
   `m_renderGraph.ListDebugVolumeTextures()` (Phase 2's new accessor),
   appending one more `PublishedTextureListEntry` per volume:

   ```cpp
   for (const rg::DebugVolumeTextureSnapshot& vol : m_renderGraph.ListDebugVolumeTextures()) {
       PublishedTextureListEntry entry;
       entry.name = vol.name;
       entry.regime = ToDebugTextureRegimeString(vol.regime); // Reuse the EXISTING helper the 2D loop already calls - same ExecuteTimingMode type, no new stringify function needed.
       entry.format = DebugTextureColorFormatName(vol.target.format); // Reuse the EXISTING helper the 2D loop already calls for its own color format.
       entry.width = vol.target.extent.width;
       entry.height = vol.target.extent.height;
       entry.hasDepth = false; // Always - see this struct's own new doc comment above.
       entry.framesSinceUpdate = currentFrameCounter - vol.lastUpdatedFrameCounter; // Same shared counter/subtraction shape the existing 2D loop already uses.
       entry.kind = "texture3d";
       entry.depth = vol.target.extent.depth;
       entries.push_back(std::move(entry));
   }
   ```

   (Adjust local variable names — `entries`/`currentFrameCounter` — to
   whatever the existing surrounding code actually calls them; this is
   illustrative, not a literal drop-in replacement. Confirm
   `DebugTextureColorFormatName()`/`ToDebugTextureRegimeString()` are
   genuinely reusable as-is for a `VkFormat`/`ExecuteTimingMode` value
   coming from a `VolumeTarget`/volume snapshot rather than a
   `RenderTarget`/2D snapshot — they should be, since both take the same
   underlying enum/format types, but verify signatures before assuming.)

### 3.3 — `NetworkRoutes.h`/`.cpp`: extend `TextureListEntryView` + JSON output

Mirror 3.1's two new fields exactly (`std::string kind = "texture2d";`,
`std::uint32_t depth = 0;`), and update `BuildListTexturesResponseJson()`'s
`nlohmann::json` construction to always emit both:

```json
{"name":"AtmosphereAerialPerspectiveVolume_GameView","regime":"synchronous","format":"R16G16B16A16_SFLOAT","width":128,"height":128,"has_depth":false,"frames_since_update":0,"kind":"texture3d","depth":32}
```

An existing 2D entry's JSON shape gains exactly the two new keys
(`"kind":"texture2d","depth":0`) and nothing else changes — update this
function's own doc-comment example JSON blob (currently showing only a 2D
shape) to show BOTH an existing 2D entry and a new 3D entry side by side,
so a future reader immediately sees both shapes without having to infer the
3D one.

### 3.4 — `NetworkServer.cpp`: copy the two new fields

**Correction (accuracy check against the real source): this is NOT a
field-by-field `view.xxx = entry.xxx;` assignment loop** — the real code
(`NetworkServer.cpp`'s `RegisterListTexturesRoute()`) builds each
`TextureListEntryView` via a single POSITIONAL aggregate initializer inside
`views.push_back(...)`:

```cpp
views.push_back(TextureListEntryView{
    entry.name, entry.regime, entry.format, entry.width, entry.height, entry.hasDepth,
    entry.framesSinceUpdate });
```

Since `kind`/`depth` are appended as new TRAILING members of both
`PublishedTextureListEntry` (3.1) and `TextureListEntryView` (3.3), in that
same order, the fix is to append two more positional values to this EXACT
initializer list, in declared field order — do NOT add separate `view.kind =
...`/`view.depth = ...` assignment statements (there is no such statement
style at this call site to begin with, and introducing one here would be an
inconsistent, unnecessary style change):

```cpp
views.push_back(TextureListEntryView{
    entry.name, entry.regime, entry.format, entry.width, entry.height, entry.hasDepth,
    entry.framesSinceUpdate, entry.kind, entry.depth });
```

Double-check `TextureListEntryView`'s own member declaration order (3.3)
lists `kind`/`depth` immediately after `framesSinceUpdate`, in that exact
order, or this positional initializer will silently assign the wrong values
to the wrong fields (a plain aggregate initializer has no field-name
checking at all) — this is exactly the kind of mistake this note exists to
prevent.

### 3.5 — Do not touch `GET /get_texture`'s own response shape

`BuildTextureCaptureJsonBody()` (the per-capture JSON envelope,
`{"width":...,"height":...,"format":"png","data_base64":"...",
"frames_since_update":...}`) is UNRELATED to this phase — it already works
correctly for a volume capture as-is (Phase 4 already produces a
`CapturedPngImage` with the right `width`/`height`/`framesSinceUpdate`,
and this function has never needed to know the SOURCE resource's kind at
all, only the resulting PNG's own dimensions). Do not add a `kind` field
here — that would be scope creep past what this phase (or the campaign)
actually needs.

### Verification

- Fast compile check: build `gte_core` + the full `GreatTamanaEngine` app
  target (same reasoning as Phase 4 — this is real, testable runtime
  behavior).
- Real runtime smoke test:
  1. `run_app_background` the engine, let a few frames render.
  2. `gte_send_request` against `GET /list_textures` and confirm the
     response now includes BOTH `"kind":"texture2d"` entries (every
     pre-existing texture, unchanged in every other field) AND at least
     one `"kind":"texture3d"` entry for
     `"AtmosphereAerialPerspectiveVolume_GameView"` (and, if the Scene
     view panel is visible this session, `"..._SceneView"` too) with a
     correct, non-zero `"depth"` value matching the Atmosphere volume's
     real Z dimension (32).
  3. Confirm every pre-existing 2D entry's OTHER fields
     (`name`/`regime`/`format`/`width`/`height`/`has_depth`/
     `frames_since_update`) are byte-for-byte identical to what they
     reported before this phase (paste a captured response from Phase 4's
     own smoke test, if you saved one, and diff by eye).
  4. `stop_app_background`.
- Write `PHASE5_COMPLETION_REPORT.md`, commit.
