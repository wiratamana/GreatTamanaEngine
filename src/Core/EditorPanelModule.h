#pragma once

// Editor panel capability for plugins and Project Assembly editor DLLs.

namespace gte {

class IPluginPanelDrawContext;

// A plugin implements this to own one dockable editor panel. Queried via
// IPluginModule::QueryCapability("IEditorPanelModule_v1") by the editor only; a standalone host never asks.
class IEditorPanelModule_v1 {
public:
    virtual ~IEditorPanelModule_v1() = default;

    // Static-duration string literal; becomes the panel's ImGui window title and dock key.
    // Must stay valid for the plugin's whole loaded lifetime.
    virtual const char* GetPanelName() const = 0;

    // Called once per frame while the panel is visible, between the editor's ImGui::Begin/End.
    // Draws only through `ctx`, never through ImGui::* directly.
    virtual void BuildPanel(IPluginPanelDrawContext& ctx) = 0;
};

inline constexpr const char* kIEditorPanelModule_v1_Name = "IEditorPanelModule_v1";

// The only way a plugin draws ImGui content. A plugin DLL calling ImGui::* directly would use its own
// uninitialised context, so every method is implemented host-side in the editor.
class IPluginPanelDrawContext {
public:
    virtual ~IPluginPanelDrawContext() = default;

    // Like ImGui::TextUnformatted(): no format string, the caller formats first (keeps the ABI plain).
    virtual void Text(const char* text) = 0;

    // Returns true exactly once, on the frame the button is clicked -
    // mirrors ImGui::Button()'s own real return-value contract.
    virtual bool Button(const char* label) = 0;

    virtual void Separator() = 0;
};

} // namespace gte
