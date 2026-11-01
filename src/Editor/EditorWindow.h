#pragma once

#include "../Core/EditorPanelRegistry.h"

#include <cstdint>
#include <string>

namespace gte {

struct EditorFrame;

// Where a window sits the first time it is shown, when no saved layout knows it yet.
enum class WindowDock : std::uint8_t { Floating, Left, Center, Right, Bottom };

// Base of every editor window (like Unity's EditorWindow). It owns the title bar, the close button, the open state and the
// Window menu entry. A window only draws its content in OnGui(). Never collapsible; always dockable.
class EditorWindow {
public:
    virtual ~EditorWindow() = default;

    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    const char* Name() const noexcept { return m_name.c_str(); }
    WindowDock DefaultDock() const noexcept { return m_dock; }

    bool IsOpen() const noexcept { return m_open; }

    // Open and its content was drawn on the last frame (false for a background tab).
    bool IsVisible() const noexcept { return m_open && m_visible; }

    // Opens the window, raises it and, when it floats, centers it on the main window.
    void Show() noexcept
    {
        m_open = true;
        m_showRequested = true;
    }

    void Close() noexcept
    {
        m_open = false;
        m_showRequested = false;
    }

    // Called once per frame by the editor layer, in registration order.
    void Draw(EditorFrame& frame);

protected:
    EditorWindow(const char* name, WindowDock dock, bool openByDefault = true, float defaultWidth = 0.0f, float defaultHeight = 0.0f)
        : m_name(name)
        , m_dock(dock)
        , m_open(openByDefault)
        , m_visible(openByDefault)
        , m_defaultWidth(defaultWidth)
        , m_defaultHeight(defaultHeight)
    {
    }

    // Runs every frame before the window is drawn, even when it is closed or hidden.
    virtual void OnFrame(EditorFrame& /*frame*/) {}

    // Draws the content. Runs only while the window is open and its content is visible.
    virtual void OnGui(EditorFrame& frame) = 0;

    // Runs instead of OnGui() when the window is closed or its tab is not selected.
    virtual void OnHidden(EditorFrame& /*frame*/) {}

    // Extra ImGuiWindowFlags_* values. NoCollapse is always added and cannot be removed.
    virtual int ExtraFlags() const { return 0; }

private:
    bool IsDockedNow() const;
    void DrawTabMenu();

    std::string m_name;
    WindowDock m_dock;
    bool m_open;
    bool m_visible;
    bool m_showRequested = false;
    float m_defaultWidth;
    float m_defaultHeight;
    std::uint32_t m_profileName = 0;
    bool m_hasProfileName = false;
};

// Adds the window to the registry (non-owning). The window must be unregistered before it is destroyed.
inline void RegisterEditorWindow(EditorWindow& window)
{
    EditorPanelRegistry::Instance().RegisterWindow(window.Name(), &window);
}

// Removes the window, but only if this exact object is the one registered under its name.
inline void UnregisterEditorWindow(EditorWindow& window)
{
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    if (registry.FindWindow(window.Name()) == &window) {
        registry.UnregisterWindow(window.Name());
    }
}

inline char LowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive ASCII order, used to sort the Window menu.
inline bool WindowNameLess(const char* a, const char* b) noexcept
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        const char la = LowerAscii(*a);
        const char lb = LowerAscii(*b);
        if (la != lb) {
            return la < lb;
        }
    }
    return *a == '\0' && *b != '\0';
}

} // namespace gte
