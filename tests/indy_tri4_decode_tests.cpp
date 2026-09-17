#include <gtest/gtest.h>

#include "fast/interpreter.h"

// Desktop, no-hardware-needed decode tests for the Rare "Indy" engine (GE/PD) G_TRI4 opcode.
// See issue #28: these cover the one behavior confirmed shared by both games' decomp headers
// (identical 4-bit-packed vertex indices, all-zero triangles skipped) without needing a full
// Interpreter/GfxRenderingAPI instance, since the decode is a pure function of the two command
// words.

namespace Fast {
namespace {

TEST(IndyTri4Decode, UnpacksFourTrianglesFromPackedNibbles) {
    // w0 low 16 bits: v0=1 v1=2 v2=3 v3=4
    // w1: v4=5 v5=6 v6=7 v7=8 v8=9 v9=A v10=B v11=C
    const IndyTri4Vertices triangles = DecodeIndyTri4Vertices(0x00001234u, 0x56789ABCu);

    EXPECT_EQ(triangles[0], (std::array<uint8_t, 3>{ 1, 2, 3 }));
    EXPECT_EQ(triangles[1], (std::array<uint8_t, 3>{ 4, 5, 6 }));
    EXPECT_EQ(triangles[2], (std::array<uint8_t, 3>{ 7, 8, 9 }));
    EXPECT_EQ(triangles[3], (std::array<uint8_t, 3>{ 0xA, 0xB, 0xC }));
}

TEST(IndyTri4Decode, IgnoresHighBitsOutsideTheNibbleFields) {
    // The opcode byte (bits 24-31) and flag byte (bits 16-23) of w0 must not leak into the
    // decoded vertex indices.
    const IndyTri4Vertices triangles = DecodeIndyTri4Vertices(0xB1FF1234u, 0x56789ABCu);

    EXPECT_EQ(triangles[0], (std::array<uint8_t, 3>{ 1, 2, 3 }));
    EXPECT_EQ(triangles[1], (std::array<uint8_t, 3>{ 4, 5, 6 }));
}

TEST(IndyTri4Decode, MaxVertexIndexIsFifteen) {
    const IndyTri4Vertices triangles = DecodeIndyTri4Vertices(0x0000FFFFu, 0xFFFFFFFFu);

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
    EXPECT_FALSE(IsIndyTri4TriangleDrawn({ 0, 0, 0 }));
}

} // namespace
} // namespace Fast
