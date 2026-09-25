// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.6) - Tier-1 tests for
// PluginRenderPassBuilderAdapter_v3's own PURE validation logic. No live
// VkDevice/Renderer involved anywhere - mirrors
// tests/Core/Plugins/PluginRenderResourceTranslationTests.cpp's own PHASE1
// precedent for this exact class of "GPU-owning class's own pure decision
// points get extracted and tested separately" discipline (AGENTS.md,
// "Testability & Regression Safety").

#include "Core/Plugins/PluginRenderPassBuilderAdapterV3Validation.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// --- IsPluginResourceHandleValid() ----------------------------------------

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, HandleWithMatchingGenerationAndInRangeIndexIsValid)
{
    EXPECT_TRUE(IsPluginResourceHandleValid(/*handleGeneration=*/7, /*handleIndex=*/2,
        /*currentAdapterGeneration=*/7, /*tableSize=*/3));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, HandleWithMismatchedGenerationIsRejected)
{
    EXPECT_FALSE(IsPluginResourceHandleValid(/*handleGeneration=*/6, /*handleIndex=*/0,
        /*currentAdapterGeneration=*/7, /*tableSize=*/3));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, HandleWithOutOfRangeIndexIsRejected)
{
    EXPECT_FALSE(IsPluginResourceHandleValid(/*handleGeneration=*/7, /*handleIndex=*/3,
        /*currentAdapterGeneration=*/7, /*tableSize=*/3));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, HandleIndexExactlyAtTableSizeIsRejected)
{
    // tableSize == 3 means valid indices are 0, 1, 2 - index 3 is one past the end.
    EXPECT_FALSE(IsPluginResourceHandleValid(/*handleGeneration=*/1, /*handleIndex=*/3,
        /*currentAdapterGeneration=*/1, /*tableSize=*/3));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, HandleAgainstEmptyTableIsAlwaysRejected)
{
    EXPECT_FALSE(IsPluginResourceHandleValid(/*handleGeneration=*/1, /*handleIndex=*/0,
        /*currentAdapterGeneration=*/1, /*tableSize=*/0));
}

// --- IsWithinResourceCreationCountCap() - 31 vs. 32 vs. 33 boundary --------

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, ThirtyFirstResourceCreationCallIsAllowed)
{
    // alreadyCreatedCount == 30 means this would be the 31st call.
    EXPECT_TRUE(IsWithinResourceCreationCountCap(30));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, ThirtySecondResourceCreationCallIsAllowed)
{
    // alreadyCreatedCount == 31 means this would be the 32nd call - the cap itself.
    EXPECT_TRUE(IsWithinResourceCreationCountCap(31));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, ThirtyThirdResourceCreationCallIsRefused)
{
    // alreadyCreatedCount == 32 means this would be the 33rd call - over the cap.
    EXPECT_FALSE(IsWithinResourceCreationCountCap(32));
}

// --- IsWithinTextureDimensionCap() - 8192 vs. 8193 boundary, either axis ---

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, ExactlyMaxTextureDimensionIsAllowed)
{
    EXPECT_TRUE(IsWithinTextureDimensionCap(8192, 8192));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, WidthOneOverMaxTextureDimensionIsRefused)
{
    EXPECT_FALSE(IsWithinTextureDimensionCap(8193, 4096));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, HeightOneOverMaxTextureDimensionIsRefused)
{
    EXPECT_FALSE(IsWithinTextureDimensionCap(4096, 8193));
}

// --- IsWithinDispatchGroupCountCap() - 64 vs. 65 boundary, per axis --------

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, ExactlyMaxGroupCountOnEveryAxisIsAllowed)
{
    EXPECT_TRUE(IsWithinDispatchGroupCountCap(64, 64, 64));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, GroupsXOneOverCapIsRefused)
{
    EXPECT_FALSE(IsWithinDispatchGroupCountCap(65, 1, 1));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, GroupsYOneOverCapIsRefused)
{
    EXPECT_FALSE(IsWithinDispatchGroupCountCap(1, 65, 1));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, GroupsZOneOverCapIsRefused)
{
    EXPECT_FALSE(IsWithinDispatchGroupCountCap(1, 1, 65));
}

// --- IsExactParamSizeMatch() -----------------------------------------------

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, ExactlyEqualParamSizeIsAccepted)
{
    EXPECT_TRUE(IsExactParamSizeMatch(64, 64));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, SmallerParamSizeThanExpectedIsRejected)
{
    EXPECT_FALSE(IsExactParamSizeMatch(60, 64));
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, LargerParamSizeThanExpectedIsRejected)
{
    EXPECT_FALSE(IsExactParamSizeMatch(68, 64));
}

// --- ValidatePluginOpSlotBinding() - one case per rejection rule -----------

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, UnboundSlotIsRejectedAsUnbound)
{
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/false,
        /*bindingHasValue=*/false, /*bindingIsBuffer=*/false, /*isCombinedImageSamplerSlot=*/true,
        /*boundIsPrivateOutputTarget=*/false);
    EXPECT_EQ(result, PluginOpSlotBindingResult::Unbound);
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, TextureBoundAgainstBufferSlotIsWrongKind)
{
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/true,
        /*bindingHasValue=*/true, /*bindingIsBuffer=*/false, /*isCombinedImageSamplerSlot=*/false,
        /*boundIsPrivateOutputTarget=*/false);
    EXPECT_EQ(result, PluginOpSlotBindingResult::WrongKind);
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, BufferBoundAgainstTextureSlotIsWrongKind)
{
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/false,
        /*bindingHasValue=*/true, /*bindingIsBuffer=*/true, /*isCombinedImageSamplerSlot=*/false,
        /*boundIsPrivateOutputTarget=*/false);
    EXPECT_EQ(result, PluginOpSlotBindingResult::WrongKind);
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, PrivateOutputTargetBoundAsCombinedImageSamplerIsRejected)
{
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/false,
        /*bindingHasValue=*/true, /*bindingIsBuffer=*/false, /*isCombinedImageSamplerSlot=*/true,
        /*boundIsPrivateOutputTarget=*/true);
    EXPECT_EQ(result, PluginOpSlotBindingResult::PrivateOutputTargetAsSampler);
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, PrivateOutputTargetBoundAsStorageImageIsStillOk)
{
    // The "cannot be sampled back" rule is specific to a CombinedImageSampler
    // slot - a plugin's own private output target CAN legitimately be bound
    // as a plain StorageImage write target (that is exactly how
    // GetPrivateOutputTarget()'s own result is normally used).
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/false,
        /*bindingHasValue=*/true, /*bindingIsBuffer=*/false, /*isCombinedImageSamplerSlot=*/false,
        /*boundIsPrivateOutputTarget=*/true);
    EXPECT_EQ(result, PluginOpSlotBindingResult::Ok);
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, CorrectlyBoundNonPrivateTextureSlotIsOk)
{
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/false,
        /*bindingHasValue=*/true, /*bindingIsBuffer=*/false, /*isCombinedImageSamplerSlot=*/true,
        /*boundIsPrivateOutputTarget=*/false);
    EXPECT_EQ(result, PluginOpSlotBindingResult::Ok);
}

TEST(PluginRenderPassBuilderAdapterV3ValidationTest, CorrectlyBoundBufferSlotIsOk)
{
    const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(/*slotIsBuffer=*/true,
        /*bindingHasValue=*/true, /*bindingIsBuffer=*/true, /*isCombinedImageSamplerSlot=*/false,
        /*boundIsPrivateOutputTarget=*/false);
    EXPECT_EQ(result, PluginOpSlotBindingResult::Ok);
}

} // namespace
} // namespace gte
