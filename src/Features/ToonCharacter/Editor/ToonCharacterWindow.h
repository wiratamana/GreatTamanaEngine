#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class ToonCharacterFeature;

// The "ToonCharacter" settings window.
class ToonCharacterWindow final : public EditorWindow {
public:
    explicit ToonCharacterWindow(ToonCharacterFeature& feature)
        : EditorWindow("ToonCharacter", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    ToonCharacterFeature& m_feature;
};

} // namespace gte
