# PHASE5 — Right-hand inspector pane: frame-level static chrome

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (`FrameDebuggerRenderTargetInfo`), PHASE4 (the
inspector `BeginChild`/`EndChild` region exists to build into).
Touches: `src/Editor/Panels/FrameDebuggerPanel.h/.cpp` only.

## Step 1: The Goal

Build the TOP portion of the right-hand inspector pane — the part of the
reference screenshot that is scoped to the **whole frame**, not to any
one selected event: a RenderTarget selector row, a Channels
(All/R/G/B/A) toggle row, a Levels slider, and a texture-preview
placeholder box with a dimension/format caption. All of it is real,
always-visible widget structure once "Enable" is checked — completely
independent of whether any event is currently selected (see
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #2 for exactly why
this frame-level chrome is treated differently from the event-level
section PHASE6 adds below it).

## Step 2: The Situation

- Reference screenshot layout (top-to-bottom, right pane): a row reading
  `RenderTarget    <No name>`; a row with a `RT 0` dropdown, `Channels`
  label, `All R G B A` toggle buttons; a `Levels` label with a slider; a
  `866x487  Default` caption; a large preview image area below it. This
  phase reproduces that exact row order using `FrameDebuggerRenderTargetInfo`
  (PHASE1) for the always-`"<No name>"`/`0x0`/`"Default"` placeholder
  values.
- `ImGui::SmallButton()`/`ImGui::Button()` with manually-tracked
  "currently active" highlighting (via `ImGui::PushStyleColor(ImGuiCol_Button,
  ...)`/`PopStyleColor()`) is the simplest way to build the "All R G B A"
  channel toggle row without a real dependency on any actual channel-mask
  concept yet — this phase's toggles are cosmetic (see 3.2 below); no
  real texture channel isolation exists this campaign.
- A placeholder texture-preview box: `ImGui::BeginChild("...", size,
  true)` with a fixed/proportional size, centered text via
  `ImGui::GetContentRegionAvail()` + `ImGui::SetCursorPos()` math (or the
  simpler `ImGui::Dummy(size)` plus an overlaid centered
  `ImGui::TextUnformatted` — either is acceptable; prefer whichever
  keeps the code shortest) reading "No Texture" — there is no real
  `RenderTexture`/`VkDescriptorSet` to sample from this campaign.

## Step 3: The Plan

### 3.1 `FrameDebuggerPanel.h` — new method

```cpp
private:
    ...
    void BuildInspectorPane(const FrameDebuggerSnapshot& snapshot);
```

(No new persistent member fields are needed this phase — the channel
toggle row's "currently selected channel" state is cosmetic-only and can
be a local `static int selectedChannel = 0;`-free, purely-visual row with
no real selection concept at all yet; keep it that simple rather than
inventing a member field with no real consumer.)

### 3.2 `FrameDebuggerPanel.cpp` — frame-level chrome

```cpp
void FrameDebuggerPanel::BuildInspectorPane(const FrameDebuggerSnapshot& snapshot)
{
    // --- RenderTarget selector row (frame-level, not event-level) ---
    ImGui::TextUnformatted("RenderTarget");
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(snapshot.renderTarget.name.c_str());

    static constexpr const char* kRenderTargetItems[] = { "RT 0" };
    int rtIndex = 0;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::BeginDisabled();
    ImGui::Combo("##FrameDebuggerRenderTarget", &rtIndex, kRenderTargetItems, 1);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextUnformatted("Channels");
    ImGui::SameLine();
    // Cosmetic-only toggle row - no real channel-isolation concept
    // exists this campaign (there is no real texture to isolate a
    // channel of yet). Purely visual parity with the reference
    // screenshot; clicking these currently has no effect beyond its own
    // pressed-highlight look.
    for (const char* channelLabel : { "All", "R", "G", "B", "A" }) {
        ImGui::SameLine();
        ImGui::SmallButton(channelLabel);
    }

    // --- Levels slider (frame-level) ---
    ImGui::TextUnformatted("Levels");
    ImGui::SameLine();
    float levelsValue = 0.0f;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled();
    ImGui::SliderFloat("##FrameDebuggerLevels", &levelsValue, 0.0f, 1.0f, "");
    ImGui::EndDisabled();

    // --- Texture preview placeholder box ---
    const std::string resolutionCaption = std::to_string(snapshot.renderTarget.width) + "x"
        + std::to_string(snapshot.renderTarget.height) + "  " + snapshot.renderTarget.format;
    ImGui::TextDisabled("%s", resolutionCaption.c_str());

    const float previewHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* placeholderText = "No Texture";
        const ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (avail.x - textSize.x) * 0.5f), std::max(0.0f, (avail.y - textSize.y) * 0.5f)));
        ImGui::TextDisabled("%s", placeholderText);
    }
    ImGui::EndChild();

    ImGui::Separator();

    // PHASE6 appends BuildEventDetailsSection() right here.
}
```

### 3.3 `FrameDebuggerPanel::Build()` — wire it in

Replace PHASE4's `"(inspector pane - added in a later phase..."`
placeholder line, inside the `"FrameDebuggerInspector"` child region,
with:

```cpp
        ImGui::BeginChild("FrameDebuggerInspector", ImVec2(0.0f, paneAreaHeight), true);
        BuildInspectorPane(snapshot);
        ImGui::EndChild();
```

### 3.4 Compile check

`cmake --build build --target GreatTamanaEngine`, then a live smoke test
confirming (with "Enable" checked): the right pane now shows
"RenderTarget &lt;No name&gt;", a grayed-out "RT 0" combo, a row of
"All"/"R"/"G"/"B"/"A" small buttons, a grayed-out Levels slider, a
"0x0  Default" caption, and a bordered preview box centered with "No
Texture" text.

### 3.5 File-change inventory (this phase only)

Modified only: `src/Editor/Panels/FrameDebuggerPanel.h`,
`src/Editor/Panels/FrameDebuggerPanel.cpp`.

Write `PHASE5_COMPLETION_REPORT.md` into `task_manager/frame-debugger-2/`
when done, then `git_add`/`git_commit`.
