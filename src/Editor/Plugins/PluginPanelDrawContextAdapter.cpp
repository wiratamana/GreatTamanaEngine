#include "PluginPanelDrawContextAdapter.h"

#include <imgui.h>

namespace gte {

void PluginPanelDrawContextAdapter::Text(const char* text)
{
    ImGui::TextUnformatted(text);
}

bool PluginPanelDrawContextAdapter::Button(const char* label)
{
    return ImGui::Button(label);
}

void PluginPanelDrawContextAdapter::Separator()
{
    ImGui::Separator();
}

} // namespace gte
