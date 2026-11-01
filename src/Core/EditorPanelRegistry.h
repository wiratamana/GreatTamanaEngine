#pragma once

#include <string>
#include <vector>

namespace gte {

class EditorWindow;

// Every editor window that exists, in registration order. Core only stores the pointers; it never calls into a window.
class EditorPanelRegistry {
public:
    struct WindowEntry {
        std::string name;
        EditorWindow* window = nullptr;
    };

    static EditorPanelRegistry& Instance();

    // Non-owning. Refuses (and logs) a null window, an empty name and a name that is already taken.
    void RegisterWindow(const std::string& name, EditorWindow* window);

    // A no-op when the name is not registered. Project assembly unload calls it for every window of that project.
    void UnregisterWindow(const std::string& name);

    bool IsKnownName(const std::string& name) const noexcept;
    EditorWindow* FindWindow(const std::string& name) const noexcept;
    std::vector<std::string> AllNames() const;
    const std::vector<WindowEntry>& Windows() const noexcept { return m_windows; }

private:
    EditorPanelRegistry() = default;

    std::vector<WindowEntry> m_windows;
};

} // namespace gte
