#include <gtest/gtest.h>

#include "fast/interpreter.h"

// Desktop, no-hardware-needed decode tests for the Rare "Indy" engine (GE/PD) G_TRI4 opcode.
// Bit layout verified directly against gfx_sp_tri4() in both goldeneye-pc-port's and
// perfect_dark's port/fast3d/gfx_pc.cpp (identical in both): for triangle i (0-3),
// x = bits[8i, 8i+4) of w1, y = bits[8i+4, 8i+8) of w1, z = bits[4i, 4i+4) of w0, and a
// triangle is skipped only when x, y, and z are all 0. See issue #28.

namespace Fast {
namespace {

TEST(IndyTri4Decode, UnpacksFourTrianglesFromPackedNibbles) {
    // w0 nibbles (low to high): z0=1 z1=2 z2=3 z3=4
    // w1 bytes (low to high): (x0=5,y0=6) (x1=7,y1=8) (x2=9,y2=A) (x3=B,y3=C)
    const IndyTri4Vertices triangles = DecodeIndyTri4Vertices(0x00004321u, 0xCBA98765u);

    EXPECT_EQ(triangles[0], (std::array<uint8_t, 3>{ 5, 6, 1 }));
    EXPECT_EQ(triangles[1], (std::array<uint8_t, 3>{ 7, 8, 2 }));
    EXPECT_EQ(triangles[2], (std::array<uint8_t, 3>{ 9, 0xA, 3 }));
    EXPECT_EQ(triangles[3], (std::array<uint8_t, 3>{ 0xB, 0xC, 4 }));
}

TEST(IndyTri4Decode, MaxVertexIndexIsFifteen) {
    const IndyTri4Vertices triangles = DecodeIndyTri4Vertices(0xFFFFFFFFu, 0xFFFFFFFFu);

    for (const auto& triangle : triangles) {
        for (uint8_t index : triangle) {
            EXPECT_EQ(index, 0xF);
        }
    }
}

TEST(IndyTri4Draw, TriangleWithNonZeroVertexIsDrawn) {
    EXPECT_TRUE(IsIndyTri4TriangleDrawn({ 1, 2, 3 }));
    EXPECT_TRUE(IsIndyTri4TriangleDrawn({ 0, 0, 1 }));
}

TEST(IndyTri4Draw, AllZeroTriangleIsPaddingAndNotDrawn) {
    // Real display lists use G_TRI4 to encode quads (2 real triangles + 2 padding slots),
    // per the "the game issues gSPTri2 for quads" comment in gfx_sp_tri4() itself.
    EXPECT_FALSE(IsIndyTri4TriangleDrawn({ 0, 0, 0 }));
}

} // namespace
} // namespace Fast
