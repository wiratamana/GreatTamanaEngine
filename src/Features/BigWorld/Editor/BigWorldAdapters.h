#pragma once

// Implements the Big World ports on top of the live terrain world, water world and the feature debug endpoints.
// Each call finds its module again, so nothing here holds a pointer across frames.

#include "../../../Editor/EditorFeatureModule.h"
#include "../../../Editor/PresetFeatureSettings.h"
#include "../../../Game/BigWorld/BigWorldPorts.h"

#include <string>
#include <vector>

namespace gte {

class TerrainPortAdapter final : public bigworld::ITerrainPort {
public:
    explicit TerrainPortAdapter(const IEditorModuleDirectory& modules) noexcept : m_modules(modules) {}

    bigworld::PortResult Open(const bigworld::TerrainOpenRequest& request) override;
    bigworld::PortResult PlaceCamera(const bigworld::CameraPlacement& placement) override;
    bigworld::PortResult Close() override;
    bigworld::TerrainFacts Facts() const override;

private:
    const IEditorModuleDirectory& m_modules;
};

class WaterPortAdapter final : public bigworld::IWaterPort {
public:
    explicit WaterPortAdapter(const IEditorModuleDirectory& modules) noexcept : m_modules(modules) {}

    bigworld::PortResult AddLake(const bigworld::LakeRequest& request) override;
    bigworld::PortResult AddRiver(const bigworld::RiverRequest& request) override;
    bigworld::PortResult RemoveBody(std::uint32_t id) override;
    bigworld::WaterFacts Facts() const override;

private:
    const IEditorModuleDirectory& m_modules;
};

class FeaturePortAdapter final : public bigworld::IFeaturePort {
public:
    explicit FeaturePortAdapter(const IEditorModuleDirectory& modules) noexcept : m_modules(modules) {}

    bool Has(std::string_view feature) const override;
    bigworld::PortResult Snapshot(std::string_view feature, std::span<const std::string> keys) override;
    bigworld::PortResult Set(std::string_view feature, std::span<const bigworld::KeyValue> values) override;
    bigworld::PortResult RestoreAll() override;
    std::vector<std::optional<std::string>> Read(std::string_view feature, std::span<const std::string_view> keys) const override;

private:
    const IEditorModuleDirectory& m_modules;
    std::vector<SavedFeatureSettings> m_saved;
};

} // namespace gte
