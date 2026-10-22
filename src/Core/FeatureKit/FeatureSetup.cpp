#include "Feature.h"

#include "../Logging.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace gte::fx {

SceneSlot FeatureSetup::DeclareSceneSlot(std::string_view name, SceneSlotKind kind, std::uint32_t preferredIndex)
{
    if (m_slots == nullptr) {
        if (!m_missingRegistrarLogged) {
            m_missingRegistrarLogged = true;
            GTE_LOG_ERROR("FeatureKit", "DeclareSceneSlot('" + std::string(name) + "'): this setup has no scene registrar");
        }
        return SceneSlot{};
    }
    return m_slots->Declare(name, kind, preferredIndex);
}

void FeatureSetup::CountInViewStats(std::string passName)
{
    if (passName.empty()) {
        m_errors.emplace_back("CountInViewStats was given an empty pass name");
        return;
    }
    m_viewStatsPasses.push_back(std::move(passName));
}

void FeatureSetup::Add(Registration registration)
{
    if (registration.name.empty()) {
        m_errors.emplace_back("a registration has an empty name");
        return;
    }
    if (registration.name.size() > kMaxRegistrationNameBytes) {
        m_errors.push_back("registration name '" + registration.name + "' is longer than "
            + std::to_string(kMaxRegistrationNameBytes) + " bytes");
        return;
    }
    const bool duplicate = std::any_of(m_registrations.begin(), m_registrations.end(),
        [&](const Registration& existing) { return existing.name == registration.name; });
    if (duplicate) {
        m_errors.push_back("registration name '" + registration.name + "' is used twice by one feature");
        return;
    }
    const bool callable = std::visit([](const auto& fn) { return static_cast<bool>(fn); }, registration.callback);
    if (!callable) {
        m_errors.push_back("registration '" + registration.name + "' has no callback");
        return;
    }
    m_registrations.push_back(std::move(registration));
}

void FeatureSetup::PreOpaque(std::string name, std::int32_t priority, PreOpaqueCallback callback, Gate gate)
{
    Add(Registration{ Stage::PreOpaque, std::move(name), priority, Blend::AlphaOver, std::move(gate), std::move(callback) });
}

void FeatureSetup::Opaque(std::string name, std::int32_t priority, OpaqueCallback callback, Gate gate)
{
    Add(Registration{ Stage::Opaque, std::move(name), priority, Blend::AlphaOver, std::move(gate), std::move(callback) });
}

void FeatureSetup::PostOpaque(std::string name, std::int32_t priority, PostOpaqueCallback callback, Gate gate)
{
    Add(Registration{ Stage::PostOpaque, std::move(name), priority, Blend::AlphaOver, std::move(gate), std::move(callback) });
}

void FeatureSetup::PostTransparent(std::string name, std::int32_t priority, PostOpaqueCallback callback, Gate gate)
{
    Add(Registration{
        Stage::PostTransparent, std::move(name), priority, Blend::AlphaOver, std::move(gate), std::move(callback) });
}

void FeatureSetup::PostComposite(
    std::string name, std::int32_t priority, Blend blend, CompositeCallback callback, Gate gate)
{
    Add(Registration{ Stage::PostComposite, std::move(name), priority, blend, std::move(gate), std::move(callback) });
}

void FeatureSetup::PreUI(std::string name, std::int32_t priority, Blend blend, CompositeCallback callback, Gate gate)
{
    Add(Registration{ Stage::PreUI, std::move(name), priority, blend, std::move(gate), std::move(callback) });
}

void FeatureSetup::PreOpaqueShared(std::string name, std::int32_t priority, SharedCallback callback, Gate gate)
{
    Add(Registration{
        Stage::PreOpaqueShared, std::move(name), priority, Blend::AlphaOver, std::move(gate), std::move(callback) });
}

std::optional<std::string> SetupAndCommit(Feature& feature, IRegistrationSink& sink)
{
    FeatureSetup setup(sink.SlotRegistrar());
    try {
        feature.Setup(setup);
    } catch (const std::exception& e) {
        return std::string("Setup() threw: ") + e.what();
    } catch (...) {
        return std::string("Setup() threw an unknown exception");
    }

    if (!setup.Errors().empty()) {
        std::string message = "invalid setup:";
        for (const std::string& error : setup.Errors()) {
            message += "\n  " + error;
        }
        return message;
    }

    try {
        for (const Registration& registration : setup.Registrations()) {
            sink.Commit(registration);
        }
        for (const std::string& passName : setup.ViewStatsPasses()) {
            sink.CommitViewStatsPass(passName);
        }
    } catch (const std::exception& e) {
        return std::string(e.what());
    }
    return std::nullopt;
}

} // namespace gte::fx
