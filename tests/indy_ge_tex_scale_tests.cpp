#include <gtest/gtest.h>

#include "fast/interpreter.h"

// Issue #73: GoldenEye's RSP leaves ST scaled by the W reciprocal, which only the RDP's
// perspective divide undoes. IndyGeNonPerspectiveTexScale models that for G_TP_NONE triangles.

namespace Fast {
namespace {

TEST(IndyGeNonPerspectiveTexScale, OrthoProjectionSaturatesToHalf) {
    // guOrtho(near 1, far 10, scale 1.0): w = 1, perspnorm = 65536 * 2 / (near + far).
    const uint16_t perspNorm = (uint16_t)(65536.0f * 2.0f / 11.0f);
    EXPECT_FLOAT_EQ(IndyGeNonPerspectiveTexScale(1.0f, perspNorm), 0.5f);
}

TEST(IndyGeNonPerspectiveTexScale, UnsetPerspNormStillSaturatesAtUnitW) {
    EXPECT_FLOAT_EQ(IndyGeNonPerspectiveTexScale(1.0f, 0xFFFF), 0.5f);
}

TEST(IndyGeNonPerspectiveTexScale, PortraitQuarterSpans64Texels) {
    // 0x1000 in s10.5 is 128 texels unscaled; the hardware-verified span is 64.
    const float texels = (0x1000 / 32.0f) * IndyGeNonPerspectiveTexScale(1.0f, 0x2E8B);
    EXPECT_FLOAT_EQ(texels, 64.0f);
}

TEST(IndyGeNonPerspectiveTexScale, RollsOffOnceNormalizedWExceedsOne) {
    EXPECT_FLOAT_EQ(IndyGeNonPerspectiveTexScale(4.0f, 0x8000), 0.25f);
}

} // namespace
} // namespace Fast
