#pragma once

// Editor-tier hooks a feature module registers: input and overlays drawn over the view images, and menu items.
// Main thread only. The hooks hold plain pointers: a registrant must remove itself before it dies.

#include "../Math/Mat4.h"
#include "../Math/Vec2.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace gte {

enum class EditorViewKind { Game, Scene };

struct EditorViewContext {
    EditorViewKind kind = EditorViewKind::Game;
    Vec2 rectMin;                 // On-screen rect of the view image.
    Vec2 rectMax;
    Mat4 viewProjection;          // Camera that drew the view.
    bool hovered = false;
    bool pointerFree = false;     // Hovered and no hook claims the pointer.
};

class IEditorViewHook {
public:
    virtual ~IEditorViewHook() = default;

    // True while the hook owns the mouse (a dragged handle), so other hooks leave it alone.
    virtual bool ClaimsPointer() const noexcept { return false; }

    // Game view only, right after the image item: read mouse and keys.
    virtual void OnViewInput(const EditorViewContext& /*view*/) {}

    // Game and Scene view: draw over the image with the current window's draw list.
    virtual void OnViewOverlay(const EditorViewContext& /*view*/) {}
};

struct EditorActionResult {
    bool ok = true;
    std::string message;
};

struct EditorMenuItem {
    std::string menu;                                 // Top-level menu title, e.g. "File".
    std::string label;
    std::function<bool()> enabled;                    // Empty means always enabled.
    std::function<EditorActionResult()> action;       // Runs on the main thread, outside the UI build.
};

// Outcome of the last menu action, shown once in the editor status line.
struct EditorMenuStatus {
    bool fresh = false;
    bool isError = false;
    std::string message;
};

class EditorFeatureHooks {
public:
    void AddViewHook(IEditorViewHook* hook);
    void RemoveViewHook(IEditorViewHook* hook);

    // Hooks run in registration order.
    void RunViewInput(EditorViewContext view) const;
    void RunViewOverlay(const EditorViewContext& view) const;

    void AddMenuItem(EditorMenuItem item);
    const std::vector<EditorMenuItem>& MenuItems() const noexcept { return m_menuItems; }
    bool MenuItemEnabled(std::size_t index) const;

    // Called from the menu: the action runs later in RunQueuedMenuActions.
    void QueueMenuAction(std::size_t index);

    // Runs the queued actions in click order. A throwing action becomes an error status.
    void RunQueuedMenuActions();

    // Returns the pending status once; fresh is false when there is none.
    EditorMenuStatus TakeMenuStatus();

private:
    std::vector<IEditorViewHook*> m_viewHooks;
    std::vector<EditorMenuItem> m_menuItems;
    std::vector<std::size_t> m_queued;
    EditorMenuStatus m_status;
};

} // namespace gte
