#pragma once

#include "Frame.h"
#include "Gate.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gte::fx {

enum class Stage : std::uint8_t {
    PreOpaque,       // Before opaque geometry, per view.
    Opaque,          // During opaque geometry, per view.
    PostOpaque,      // After opaque geometry, per view.
    PostTransparent, // After transparent geometry, per view.
    PostComposite,   // Near the end of the frame, with a private target, per view.
    PreUI,           // Same as PostComposite, right after it.
    PreOpaqueShared, // Once per frame, on the first view only.
};

// How a PostComposite or PreUI private target is blended onto the screen.
enum class Blend : std::uint8_t { Replace, AlphaOver, Additive, Multiply, ScreenSpaceMask };

// Core refuses registration names longer than this.
inline constexpr std::size_t kMaxRegistrationNameBytes = 63;

using PreOpaqueCallback = std::function<void(PreOpaqueFrame&)>;
using OpaqueCallback = std::function<void(OpaqueFrame&)>;
using PostOpaqueCallback = std::function<void(PostOpaqueFrame&)>;
using CompositeCallback = std::function<void(CompositeFrame&)>;
using SharedCallback = std::function<void(SharedFrame&)>;

// One collected registration. Nothing reaches Core until the whole Setup() has succeeded.
struct Registration {
    Stage stage = Stage::PreOpaque;
    std::string name;
    std::int32_t priority = 0;
    Blend blend = Blend::AlphaOver; // PostComposite and PreUI only.
    Gate gate;
    std::variant<PreOpaqueCallback, PostOpaqueCallback, CompositeCallback, SharedCallback, OpaqueCallback> callback;
    bool secondaryViewDefault = true; // False: starts disabled in the secondary view.
    std::string consumedChannel;      // "<owner>.<channel>" read even in views without surfaces; empty: none.
};

// Registration verbs offered to Feature::Setup(). They only collect; mistakes are recorded and make the
// whole setup fail, so a feature never ends up half registered.
class FeatureSetup {
public:
    explicit FeatureSetup(ISceneSlotRegistrar* slots = nullptr) noexcept
        : m_slots(slots)
    {
    }

    void PreOpaque(std::string name, std::int32_t priority, PreOpaqueCallback callback, Gate gate = {});
    void Opaque(std::string name, std::int32_t priority, OpaqueCallback callback, Gate gate = {});
    void PostOpaque(std::string name, std::int32_t priority, PostOpaqueCallback callback, Gate gate = {});
    void PostTransparent(std::string name, std::int32_t priority, PostOpaqueCallback callback, Gate gate = {});
    void PostComposite(std::string name, std::int32_t priority, Blend blend, CompositeCallback callback, Gate gate = {});
    void PreUI(std::string name, std::int32_t priority, Blend blend, CompositeCallback callback, Gate gate = {});
    void PreOpaqueShared(std::string name, std::int32_t priority, SharedCallback callback, Gate gate = {});

    // Whether registrations added after this call start enabled in the secondary view (default true).
    // Shared and Always() registrations are never gated.
    void DefaultSecondaryViewEnabled(bool enabled) noexcept { m_secondaryViewDefault = enabled; }

    // Marks the earlier registration `registrationName` as a reader of channel "<owner>.<channel>" that needs it even
    // in views without surfaces (it shades empty space). Fails the setup for an unknown registration.
    void ReadsChannelInEmptyViews(std::string_view registrationName, std::string_view ownerName, std::string_view channelName);

    // Declares a scene-services slot at startup. Invalid (and one log) when it cannot be declared.
    SceneSlot DeclareSceneSlot(std::string_view name, SceneSlotKind kind, std::uint32_t preferredIndex);

    // Records a setup error: the whole Setup() then fails and nothing is registered.
    void Fail(std::string message);

    const std::vector<Registration>& Registrations() const noexcept { return m_registrations; }
    const std::vector<std::string>& Errors() const noexcept { return m_errors; }

private:
    void Add(Registration registration);

    ISceneSlotRegistrar* m_slots;
    bool m_missingRegistrarLogged = false;
    bool m_secondaryViewDefault = true;
    std::vector<Registration> m_registrations;
    std::vector<std::string> m_errors;
};

} // namespace gte::fx
