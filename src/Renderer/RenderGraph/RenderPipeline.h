#pragma once

// Pass declaration layer above RenderGraphBuilder: providers hand out RenderPassDesc values that are
// sorted by RenderPassEvent and declared into the builder, translating opaque ids to legacy ViewScope.

#include "RenderFeatureScope.h"
#include "RenderGraphHandles.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphTypes.h"
#include "RenderPassToggleRegistry.h"
#include "../RenderTimingObserver.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace gte::rg {

// Stable, typed pass/blackboard-key identity: lookups are integer compares.
struct RenderPassId {
    std::uint64_t hash = 0;

    constexpr bool operator==(const RenderPassId&) const noexcept = default;
};

// FNV-1a over the literal's bytes, evaluated at compile time.
consteval RenderPassId operator""_passId(const char* s, std::size_t n) noexcept
{
    std::uint64_t hash = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i]));
        hash *= 16777619u;
    }
    return RenderPassId{ hash };
}

// Key for a string-named cross-feature handoff: a pure hash of (name, view), no registration.
RenderPassId SceneResourceKeyOf(std::string_view name, RenderViewId view) noexcept;

// A plain value describing one pass; produced by providers, consumed generically by RenderPipeline.
struct RenderPassDesc {
    RenderPassId id;
    const char* debugName = nullptr;
    PassKind kind = PassKind::Graphics;
    // Cross-checked against real dependencies and used as ordering input (see RenderPassEvent).
    RenderPassEvent order = RenderPassEvent::Opaques;
    // Set by the provider; no feature scope is open when the deferred flush runs.
    std::string owningFeatureName;
    RenderViewId view = RenderViewId::Shared();

    // Stamped onto the PassRecord so the Frame Debugger sees the same data as for a directly declared pass.
    RenderPassCategory legacyCategory = RenderPassCategory::General;
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;

    std::function<void(RenderGraphBuilder::PassBuilder&)> setup;
    std::function<void(PassContext&)> execute;
};

// Cross-provider data hand-off: an opaque-keyed slot store. Keys live in the publishing feature's header.
class RenderPassBlackboard {
public:
    // Last publish wins. Debug builds warn once per key per frame on a repeated publish.
    template <typename T>
    void Publish(RenderPassId key, T value)
    {
        for (Slot& slot : m_slots) {
            if (slot.key == key) {
#ifndef NDEBUG
                if (!slot.alreadyWarnedThisFrame) {
                    std::fprintf(stderr,
                        "RenderPassBlackboard: key %llu was Publish()'d more than once this frame - "
                        "last-publish-wins; the earlier value is now lost.\n",
                        static_cast<unsigned long long>(key.hash));
                    slot.alreadyWarnedThisFrame = true;
                }
#endif
                slot.value = std::any(std::move(value));
#ifndef NDEBUG
                slot.wasFetched = false; // A fresh publish this frame - not yet fetched again.
#endif
                return;
            }
        }

        Slot slot;
        slot.key = key;
        slot.value = std::any(std::move(value));
        m_slots.push_back(std::move(slot));
    }

    // nullopt for a never-published key or a key published under a different type than `T`.
    template <typename T>
    std::optional<T> Fetch(RenderPassId key) const
    {
        for (const Slot& slot : m_slots) {
            if (slot.key == key) {
#ifndef NDEBUG
                slot.wasFetched = true;
#endif
                if (const T* value = std::any_cast<T>(&slot.value)) {
                    return *value;
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // True if the key was published this frame, whatever its type. Does not count as a fetch.
    bool WasPublishedThisFrame(RenderPassId key) const noexcept
    {
        for (const Slot& slot : m_slots) {
            if (slot.key == key) {
                return true;
            }
        }
        return false;
    }

    // Called once at the start of each frame's declaration. Clears entries but keeps the reserved capacity.
    void BeginFrame() { m_slots.clear(); }

#ifndef NDEBUG
    // Debug only: logs (never asserts) every key published but never fetched this frame.
    void ReportUnusedPublishesIfAny() const
    {
        for (const Slot& slot : m_slots) {
            if (!slot.wasFetched) {
                std::fprintf(stderr,
                    "RenderPassBlackboard: a value was published under key %llu but never fetched this frame.\n",
                    static_cast<unsigned long long>(slot.key.hash));
            }
        }
    }
#endif

private:
    struct Slot {
        RenderPassId key;
        std::any value;
#ifndef NDEBUG
        mutable bool wasFetched = false;
        bool alreadyWarnedThisFrame = false;
#endif
    };

    // A flat vector: live keys per frame are few, so a linear scan beats a hash map.
    std::vector<Slot> m_slots;
};

struct RenderPassFrameContext {
    std::vector<RenderViewId> activeViews;
    RenderViewId currentView = RenderViewId::Shared(); // Stamped by RenderPipeline before each PerActiveView provider call.
    RenderPassBlackboard& blackboard;

    // Lets a provider declare passes directly instead of deferring them as RenderPassDesc values.
    RenderGraphBuilder& builder;

    // Final textures a provider needs kept alive as culling roots; the caller forwards them to the graph.
    // Mutable so a provider holding a const context can still append.
    mutable std::vector<TextureHandle> finalTextureOutputs;
};

using RenderPassProvider =
    std::function<void(const RenderPassFrameContext& frame, std::vector<RenderPassDesc>& outPasses)>;

// Once: invoked once per frame-declaration call.
// PerActiveView: invoked once per entry in frame.activeViews, with the current view stamped into the context.
enum class ProviderScope { Once, PerActiveView };

// Passes declared directly via frame.builder land before the deferred flush of their phase; a direct reader
// of a deferred writer's texture must use AfterDeferredPasses, or the writer is culled.
enum class ProviderTiming { BeforeDeferredPasses, AfterDeferredPasses };

// Owns the registered providers, collects their RenderPassDesc values each frame, sorts them by
// RenderPassEvent and declares them into the render graph.
class RenderPipeline {
public:
    // Duplicate names are not rejected; names the engine depends on for ordering are reserved.
    void Register(const char* debugName, ProviderScope scope, RenderPassProvider provider,
        ProviderTiming timing = ProviderTiming::BeforeDeferredPasses)
    {
        m_providers.push_back(Entry{ debugName, scope, std::move(provider), timing });
    }

    // Maps a view id to a ViewScope; without one every view is Shared.
    void SetLegacyViewScopeTranslator(std::function<ViewScope(RenderViewId)> translator)
    {
        m_legacyViewScopeTranslator = std::move(translator);
    }

    // Without a registry every pass is enabled.
    void SetPassToggleRegistry(RenderPassToggleRegistry* registry) noexcept
    {
        m_passToggleRegistry = registry;
    }

    // Optional. Sees the declaration time of each provider.
    void SetTimingObserver(IRenderTimingObserver* observer) noexcept { m_timingObserver = observer; }

    // Removes providers by debugName (pointer or content match).
    void Unregister(const char* debugName)
    {
        if (debugName == nullptr) {
            return;
        }

        for (auto it = m_providers.begin(); it != m_providers.end();) {
            const bool matches = it->debugName == debugName
                || (it->debugName != nullptr && std::strcmp(it->debugName, debugName) == 0);
            if (matches) {
                it = m_providers.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Called once per graph-build callback: runs the BeforeDeferredPasses phase, then AfterDeferredPasses.
    void DeclareInto(RenderGraphBuilder& builder, RenderPassFrameContext& frame)
    {
        DeclareOnePhase(builder, frame, ProviderTiming::BeforeDeferredPasses);
        DeclareOnePhase(builder, frame, ProviderTiming::AfterDeferredPasses);
    }

private:
    struct Entry {
        const char* debugName;
        ProviderScope scope;
        RenderPassProvider provider;
        ProviderTiming timing;
    };

    // Runs every provider of `timing`, sorts the deferred descriptions by `order` and flushes them into `builder`.
    // Once-scope providers run before PerActiveView ones so shared data is published before any view fetches it.
    void DeclareOnePhase(RenderGraphBuilder& builder, RenderPassFrameContext& frame, ProviderTiming timing)
    {
        m_scratchCollected.clear(); // Reused, not reconstructed.

        for (const Entry& entry : m_providers) {
            if (entry.timing == timing && entry.scope == ProviderScope::Once) {
                const DeclareScope observed(m_timingObserver, entry.debugName);
                entry.provider(frame, m_scratchCollected);
            }
        }
        for (const Entry& entry : m_providers) {
            if (entry.timing == timing && entry.scope == ProviderScope::PerActiveView) {
                const DeclareScope observed(m_timingObserver, entry.debugName);
                for (const RenderViewId& view : frame.activeViews) {
                    frame.currentView = view;
                    entry.provider(frame, m_scratchCollected);
                }
            }
        }

        std::stable_sort(m_scratchCollected.begin(), m_scratchCollected.end(),
            [](const RenderPassDesc& a, const RenderPassDesc& b) { return a.order < b.order; });

        for (RenderPassDesc& desc : m_scratchCollected) {
            const ViewScope translatedViewScope =
                m_legacyViewScopeTranslator ? m_legacyViewScopeTranslator(desc.view) : ViewScope::Shared;
            // The one choke point where a disabled pass is skipped, as if never declared.
            if (m_passToggleRegistry != nullptr && desc.debugName != nullptr
                && !m_passToggleRegistry->IsEnabled(desc.debugName)) {
                continue;
            }
            // Opens a feature scope around each desc from the name captured by the provider.
            const RenderFeatureScope scope(builder, desc.owningFeatureName);
            builder.AddRenderPass(desc.debugName, desc.kind, translatedViewScope, desc.legacyCategory, desc.setup,
                desc.execute, desc.drawKind, desc.order);
        }
    }

    std::vector<Entry> m_providers;
    std::function<ViewScope(RenderViewId)> m_legacyViewScopeTranslator;
    RenderPassToggleRegistry* m_passToggleRegistry = nullptr;
    IRenderTimingObserver* m_timingObserver = nullptr;

    // Cleared, not reconstructed, each DeclareInto() so its capacity survives across frames.
    std::vector<RenderPassDesc> m_scratchCollected;
};

} // namespace gte::rg
