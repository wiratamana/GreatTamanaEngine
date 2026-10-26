// Tier-1 tests for BoundedRetryGates: per-key retry back-off with a hard key cap.

#include "Renderer/RenderGraph/BoundedRetryGates.h"
#include "Renderer/RenderGraph/RenderGraphComputeBindingKey.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace gte::rg {
namespace {

using IntGates = BoundedRetryGates<int, std::hash<int>, std::equal_to<int>, 4>;
using FailureKind = BuildRetryGate::FailureKind;

TEST(BoundedRetryGatesTest, UnknownKeyIsAlwaysAttempted)
{
    const IntGates gates;
    EXPECT_TRUE(gates.ShouldAttempt(7, 0));
    EXPECT_TRUE(gates.ShouldAttempt(7, 100000));
}

TEST(BoundedRetryGatesTest, FailingKeyIsBlockedOnlyForItself)
{
    IntGates gates;
    EXPECT_EQ(gates.OnFailure(1, 10), FailureKind::First);
    EXPECT_FALSE(gates.ShouldAttempt(1, 11));
    EXPECT_TRUE(gates.ShouldAttempt(2, 11));
}

TEST(BoundedRetryGatesTest, FailingKeyIsAttemptedAgainAfterTheCooldown)
{
    IntGates gates;
    gates.OnFailure(1, 10);
    EXPECT_FALSE(gates.ShouldAttempt(1, 10 + BuildRetryGate::kInitialCooldownFrames - 1));
    EXPECT_TRUE(gates.ShouldAttempt(1, 10 + BuildRetryGate::kInitialCooldownFrames));
}

TEST(BoundedRetryGatesTest, RepeatFailureIsNotFirst)
{
    IntGates gates;
    gates.OnFailure(1, 0);
    EXPECT_EQ(gates.OnFailure(1, 200), FailureKind::Repeat);
}

TEST(BoundedRetryGatesTest, SuccessClearsTheKeyAndReportsRecovery)
{
    IntGates gates;
    gates.OnFailure(1, 0);
    EXPECT_TRUE(gates.OnSuccess(1));
    EXPECT_TRUE(gates.ShouldAttempt(1, 1));
    EXPECT_FALSE(gates.OnSuccess(1));
    EXPECT_EQ(gates.OnFailure(1, 5), FailureKind::First);
}

TEST(BoundedRetryGatesTest, FloodOfDistinctKeysNeverExceedsTheCap)
{
    IntGates gates;
    for (int key = 0; key < 100; ++key) {
        gates.OnFailure(key, 0);
    }
    std::size_t blocked = 0;
    for (int key = 0; key < 100; ++key) {
        blocked += gates.ShouldAttempt(key, 1) ? 0 : 1;
    }
    EXPECT_EQ(blocked, 4u);
}

TEST(BoundedRetryGatesTest, EraseIfDropsOnlyMatchingKeys)
{
    IntGates gates;
    gates.OnFailure(1, 0);
    gates.OnFailure(2, 0);
    gates.EraseIf([](int key) { return key == 1; });
    EXPECT_TRUE(gates.ShouldAttempt(1, 1));
    EXPECT_FALSE(gates.ShouldAttempt(2, 1));
}

// The binding cache uses the transparent key types: lookups with a view must not need a std::string key.
using BindingGates = BoundedRetryGates<ComputeBindingKey, ComputeBindingKeyHash, ComputeBindingKeyEqual, 64>;

TEST(BoundedRetryGatesTest, BindingKeyViewFindsTheOwningKey)
{
    BindingGates gates;
    const auto layout = reinterpret_cast<VkDescriptorSetLayout>(static_cast<std::uintptr_t>(0x10));
    gates.OnFailure(ComputeBindingKey{ std::string("Pass.A"), ViewScope::Primary, layout }, 5);

    const ComputeBindingKeyView same{ "Pass.A", ViewScope::Primary, layout };
    const ComputeBindingKeyView otherScope{ "Pass.A", ViewScope::Secondary, layout };
    const ComputeBindingKeyView otherLayout{ "Pass.A", ViewScope::Primary, reinterpret_cast<VkDescriptorSetLayout>(static_cast<std::uintptr_t>(0x20)) };
    EXPECT_FALSE(gates.ShouldAttempt(same, 6));
    EXPECT_TRUE(gates.ShouldAttempt(otherScope, 6));
    EXPECT_TRUE(gates.ShouldAttempt(otherLayout, 6));
}

TEST(BoundedRetryGatesTest, EraseIfByLayoutForgetsEveryPassOnThatLayout)
{
    BindingGates gates;
    const auto layoutA = reinterpret_cast<VkDescriptorSetLayout>(static_cast<std::uintptr_t>(0x10));
    const auto layoutB = reinterpret_cast<VkDescriptorSetLayout>(static_cast<std::uintptr_t>(0x20));
    gates.OnFailure(ComputeBindingKey{ "P1", ViewScope::Primary, layoutA }, 0);
    gates.OnFailure(ComputeBindingKey{ "P2", ViewScope::Secondary, layoutA }, 0);
    gates.OnFailure(ComputeBindingKey{ "P3", ViewScope::Primary, layoutB }, 0);

    gates.EraseIf([layoutA](const ComputeBindingKey& key) { return key.layout == layoutA; });

    EXPECT_TRUE(gates.ShouldAttempt(ComputeBindingKeyView{ "P1", ViewScope::Primary, layoutA }, 1));
    EXPECT_TRUE(gates.ShouldAttempt(ComputeBindingKeyView{ "P2", ViewScope::Secondary, layoutA }, 1));
    EXPECT_FALSE(gates.ShouldAttempt(ComputeBindingKeyView{ "P3", ViewScope::Primary, layoutB }, 1));
}

} // namespace
} // namespace gte::rg
