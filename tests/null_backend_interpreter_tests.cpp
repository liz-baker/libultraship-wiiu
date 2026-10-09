#include <gtest/gtest.h>

// Encode with the F3DEX2 GBI, which is the interpreter's default ucode.
#define F3DEX_GBI_2
#include "libultraship/libultra/gbi.h"
#include "support/null_gfx_backend.h"

// Issue #31: drive a real Interpreter through Run() against the null backend and check what the
// opcode handlers actually did, not just which handler the dispatch table picked.

namespace Fast {
namespace {

// Fixed-point (s15.16) matrix in the GBI layout GfxSpMatrix decodes: integer halves of each element
// pair in words 0-7, fractional halves in words 8-15.
struct FixedMtx {
    alignas(8) int32_t words[16];
};

FixedMtx IdentityMtx() {
    FixedMtx m{};
    m.words[0] = 0x00010000; // [0][0]
    m.words[2] = 0x00000001; // [1][1]
    m.words[5] = 0x00010000; // [2][2]
    m.words[7] = 0x00000001; // [3][3]
    return m;
}

TEST(NullBackendInterpreter, InitSucceedsWithoutWindowOrGpu) {
    NullBackendInterpreter h;
    EXPECT_NE(h.interpreter->GetCurrentRenderingAPI(), nullptr);
    EXPECT_STREQ(h.interpreter->GetCurrentRenderingAPI()->GetName(), "Null");
}

TEST(NullBackendInterpreter, EmptyDisplayListRuns) {
    NullBackendInterpreter h;
    Gfx dl[] = { gsSPEndDisplayList() };
    h.Run(dl);
    EXPECT_TRUE(h.rapi.draws.empty());
}

TEST(NullBackendInterpreter, VtxAndTri1LoadVerticesAndDrawOneTriangle) {
    NullBackendInterpreter h;

    FixedMtx identity = IdentityMtx();
    Vtx vtx[3] = {};
    const short pos[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const unsigned char col[3][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 } };
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) {
            vtx[i].v.ob[k] = pos[i][k];
        }
        for (int k = 0; k < 4; k++) {
            vtx[i].v.cn[k] = col[i][k];
        }
    }

    Gfx dl[] = {
        gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
        gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
        gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
        gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH),
        gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
        gsSPVertex(vtx, 3, 0),
        gsSP1Triangle(0, 1, 2, 0),
        gsSPEndDisplayList(),
    };
    h.Run(dl);

    const LoadedVertex* lv = h.interpreter->mRsp->loaded_vertices;
    for (int i = 0; i < 3; i++) {
        SCOPED_TRACE(i);
        EXPECT_FLOAT_EQ(lv[i].x, pos[i][0]);
        EXPECT_FLOAT_EQ(lv[i].y, pos[i][1]);
        EXPECT_FLOAT_EQ(lv[i].z, pos[i][2]);
        EXPECT_FLOAT_EQ(lv[i].w, 1.0f);
        EXPECT_EQ(lv[i].color.r, col[i][0]);
        EXPECT_EQ(lv[i].color.g, col[i][1]);
        EXPECT_EQ(lv[i].color.b, col[i][2]);
    }

    size_t tris = 0;
    for (const auto& d : h.rapi.draws) {
        tris += d.numTris;
    }
    EXPECT_EQ(tris, 1u);
    EXPECT_EQ(h.rapi.shadersCreated.size(), 1u);
}

} // namespace
} // namespace Fast
