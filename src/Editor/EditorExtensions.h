#pragma once

#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace gte {

// One slot per type, so a feature can keep UI state in the editor context without the context knowing the type.
// Main thread only.
class EditorExtensions {
public:
    // The slot of type T; default-constructed on first use. The reference stays valid for the life of this object.
    template <typename T>
    T& Get()
    {
        std::shared_ptr<void>& slot = m_slots[std::type_index(typeid(T))];
        if (!slot) {
            slot = std::make_shared<T>();
        }
        return *static_cast<T*>(slot.get());
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> m_slots;
};

} // namespace gte
