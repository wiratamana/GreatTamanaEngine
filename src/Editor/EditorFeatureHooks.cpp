#include "EditorFeatureHooks.h"

#include "../Profiling/ScopeTimer.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace gte {

void EditorFeatureHooks::AddViewHook(IEditorViewHook* hook)
{
    if (hook != nullptr && std::find(m_viewHooks.begin(), m_viewHooks.end(), hook) == m_viewHooks.end()) {
        m_viewHooks.push_back(hook);
    }
}

void EditorFeatureHooks::RemoveViewHook(IEditorViewHook* hook)
{
    m_viewHooks.erase(std::remove(m_viewHooks.begin(), m_viewHooks.end(), hook), m_viewHooks.end());
}

void EditorFeatureHooks::RunViewInput(EditorViewContext view) const
{
    GTE_PROFILE_SCOPE("EditorFeatureHooks::RunViewInput");
    const bool claimed = std::any_of(m_viewHooks.begin(), m_viewHooks.end(),
        [](const IEditorViewHook* hook) { return hook->ClaimsPointer(); });
    view.pointerFree = view.hovered && !claimed;
    for (IEditorViewHook* hook : m_viewHooks) {
        hook->OnViewInput(view);
    }
}

void EditorFeatureHooks::RunViewOverlay(const EditorViewContext& view) const
{
    GTE_PROFILE_SCOPE("EditorFeatureHooks::RunViewOverlay");
    for (IEditorViewHook* hook : m_viewHooks) {
        hook->OnViewOverlay(view);
    }
}

void EditorFeatureHooks::AddMenuItem(EditorMenuItem item)
{
    if (item.action) {
        m_menuItems.push_back(std::move(item));
    }
}

bool EditorFeatureHooks::MenuItemEnabled(std::size_t index) const
{
    return index < m_menuItems.size() && (!m_menuItems[index].enabled || m_menuItems[index].enabled());
}

void EditorFeatureHooks::QueueMenuAction(std::size_t index)
{
    if (index < m_menuItems.size()) {
        m_queued.push_back(index);
    }
}

void EditorFeatureHooks::RunQueuedMenuActions()
{
    // Take the queue first: an action may queue another click.
    std::vector<std::size_t> queued;
    queued.swap(m_queued);
    for (const std::size_t index : queued) {
        // Copy: the action may add menu items and move the vector.
        const std::function<EditorActionResult()> action = m_menuItems[index].action;
        const std::string label = m_menuItems[index].label;
        EditorActionResult result;
        try {
            result = action();
        } catch (const std::exception& e) {
            result = { false, label + " failed: " + e.what() };
        } catch (...) {
            result = { false, label + " failed." };
        }
        m_status = { true, !result.ok, std::move(result.message) };
    }
}

EditorMenuStatus EditorFeatureHooks::TakeMenuStatus()
{
    EditorMenuStatus out = std::move(m_status);
    m_status = {};
    return out;
}

} // namespace gte
