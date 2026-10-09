#include <gtest/gtest.h>

#include <array>
#include <vector>

// Encode with the F3DEX2 GBI, which is the interpreter's default ucode.
#define F3DEX_GBI_2
#include "libultraship/libultra/gbi.h"
#include "support/null_gfx_backend.h"

// Issue #79: G_TD_DETAIL / G_TD_SHARPEN compute LOD_FRACTION per pixel in the shader.

namespace Fast {
namespace {

struct FixedMtx {
    alignas(8) int32_t words[16];
};

FixedMtx IdentityMtx() {
    FixedMtx m{};
    m.words[0] = 0x00010000;
    m.words[2] = 0x00000001;
    m.words[5] = 0x00010000;
    m.words[7] = 0x00000001;
    return m;
}

// RGBA16 base chain (8x8 down to 1x1) loaded by one G_LOADBLOCK; offsets relative to its TMEM base.
struct Level {
    uint32_t width, height, tmemWords, lineWords;
};
constexpr std::array<Level, 4> kChain = { {
    { 8, 8, 0, 2 },
    { 4, 4, 16, 1 },
    { 2, 2, 20, 1 },
    { 1, 1, 22, 1 },
} };
constexpr uint32_t kChainWords = 23;
// A 4x4 RGBA16 detail texture: one TMEM word per row.
constexpr uint32_t kDetailWords = 4;
// Where the base chain goes when a detail texture sits at TMEM 0.
constexpr uint32_t kChainTmemAfterDetail = 64;

constexpr uint8_t kPrimLodMin = 8; // 8/32 = 0.25 texels per pixel

class TextureDetailTest : public testing::Test {
  protected:
    void SetUp() override {
        chainTexels.fill(0xFF);
        detailTexels.fill(0x80);
        const short pos[3][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
        for (int i = 0; i < 3; i++) {
            vtx[i].v.ob[0] = pos[i][0];
            vtx[i].v.ob[1] = pos[i][1];
            vtx[i].v.cn[3] = 255;
        }
    }

    // The base chain's tiles start at `baseTile`, its block at TMEM `chainTmem`. With `withDetail`,
    // tile 0 is a separately loaded 4x4 detail texture at TMEM 0.
    std::vector<Gfx> DisplayList(uint32_t detailMode, uint32_t cycleType, bool withDetail, uint8_t maxLevel = 3) {
        const uint8_t baseTile = withDetail ? 1 : 0;
        const uint32_t chainTmem = withDetail ? kChainTmemAfterDetail : 0;
        std::vector<Gfx> dl = {
            gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
            gsDPSetCycleType(cycleType),
            gsDPSetTextureLOD(G_TL_LOD),
            gsDPSetTextureDetail(detailMode),
            gsDPSetPrimColor(kPrimLodMin, 0, 255, 255, 255, 255),
            // Detail-style blend: (TEXEL0 - TEXEL1) * LOD_FRACTION + TEXEL1, then pass through.
            gsDPSetCombineLERP(TEXEL0, TEXEL1, LOD_FRACTION, TEXEL1, TEXEL0, TEXEL1, LOD_FRACTION, TEXEL1, COMBINED, 0,
                               SHADE, 0, COMBINED, 0, SHADE, 0),
            gsSPTexture(0xFFFF, 0xFFFF, maxLevel, G_TX_RENDERTILE, G_ON),
        };
        if (withDetail) {
            dl.push_back(gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, detailTexels.data()));
            dl.push_back(gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0));
            dl.push_back(gsDPLoadSync());
            dl.push_back(gsDPLoadBlock(G_TX_LOADTILE, 0, 0, kDetailWords * 4 - 1, 0));
            dl.push_back(gsDPPipeSync());
            dl.push_back(gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, 0, 0, G_TX_WRAP, 2, 0, G_TX_WRAP, 2, 0));
            dl.push_back(gsDPSetTileSize(0, 0, 0, 3 << G_TEXTURE_IMAGE_FRAC, 3 << G_TEXTURE_IMAGE_FRAC));
        }
        dl.push_back(gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, chainTexels.data()));
        dl.push_back(gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, chainTmem, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0));
        dl.push_back(gsDPLoadSync());
        dl.push_back(gsDPLoadBlock(G_TX_LOADTILE, 0, 0, kChainWords * 4 - 1, 0));
        dl.push_back(gsDPPipeSync());
        for (uint8_t n = 0; n < kChain.size(); n++) {
            const auto& level = kChain[n];
            uint8_t tile = baseTile + n;
            dl.push_back(gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, level.lineWords, chainTmem + level.tmemWords, tile, 0,
                                     G_TX_WRAP, 3 - n, n, G_TX_WRAP, 3 - n, n));
            dl.push_back(gsDPSetTileSize(tile, 0, 0, (level.width - 1) << G_TEXTURE_IMAGE_FRAC,
                                         (level.height - 1) << G_TEXTURE_IMAGE_FRAC));
        }
        dl.push_back(gsSPVertex(vtx, 3, 0));
        dl.push_back(gsSP1Triangle(0, 1, 2, 0));
        dl.push_back(gsSPEndDisplayList());
        return dl;
    }

    void Run(std::vector<Gfx> dl) {
        h.Run(dl.data());
    }

    CCFeatures DrawnShaderFeatures() {
        EXPECT_FALSE(h.rapi.draws.empty());
        CCFeatures features{};
        if (!h.rapi.draws.empty()) {
            gfx_cc_get_features(h.rapi.draws.back().shaderId0, h.rapi.draws.back().shaderId1, &features);
        }
        return features;
    }

    NullBackendInterpreter h;
    FixedMtx identity = IdentityMtx();
    Vtx vtx[3] = {};
    std::array<uint8_t, kChainWords * 8> chainTexels{};
    std::array<uint8_t, kDetailWords * 8> detailTexels{};
};

TEST_F(TextureDetailTest, DetailComputesLodFractionInTheShader) {
    Run(DisplayList(G_TD_DETAIL, G_CYC_2CYCLE, true));

    CCFeatures features = DrawnShaderFeatures();
    EXPECT_TRUE(features.opt_lod_detail);
    EXPECT_FALSE(features.opt_lod_sharpen);
    // Cycle 1's colour and alpha C inputs are the per-pixel fraction, not a vertex input.
    EXPECT_EQ(features.c[0][0][2], SHADER_LOD_FRACTION);
    EXPECT_EQ(features.c[0][1][2], SHADER_LOD_FRACTION);
}

TEST_F(TextureDetailTest, DetailTileIsSlot0AndTheChainSlot1) {
    Run(DisplayList(G_TD_DETAIL, G_CYC_2CYCLE, true));

    auto* slot0 = h.interpreter->mRenderingState.mTextures[0];
    auto* slot1 = h.interpreter->mRenderingState.mTextures[1];
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    EXPECT_NE(slot0->second.texture_id, slot1->second.texture_id);
    EXPECT_EQ(slot0->first.mip_levels, 0);
    EXPECT_EQ(slot0->first.texture_addr, detailTexels.data());
    EXPECT_EQ(slot1->first.mip_levels, 4);
    EXPECT_EQ(slot1->first.texture_addr, chainTexels.data());

    size_t chainUploads = 0;
    for (const auto& up : h.rapi.uploads) {
        if (!up.lowerLevels.empty()) {
            chainUploads++;
            EXPECT_EQ(up.width, 8u);
            EXPECT_EQ(up.lowerLevels.size(), 3u);
        } else {
            EXPECT_EQ(up.width, 4u);
            EXPECT_EQ(up.height, 4u);
        }
    }
    EXPECT_EQ(chainUploads, 1u);
}

TEST_F(TextureDetailTest, DetailWithoutAChainStillBlendsTheDetail) {
    // Max level 0: the base is a single level, but magnified pixels still blend the detail in.
    Run(DisplayList(G_TD_DETAIL, G_CYC_2CYCLE, true, /*maxLevel=*/0));

    EXPECT_TRUE(DrawnShaderFeatures().opt_lod_detail);
    for (const auto& up : h.rapi.uploads) {
        EXPECT_TRUE(up.lowerLevels.empty());
    }
}

TEST_F(TextureDetailTest, PrimLodMinimumReachesTheBackend) {
    Run(DisplayList(G_TD_DETAIL, G_CYC_2CYCLE, true));

    ASSERT_EQ(h.rapi.primLodMinCalls.size(), 1u);
    EXPECT_FLOAT_EQ(h.rapi.primLodMinCalls[0], kPrimLodMin / 32.0f);

    // Unchanged on the next frame: nothing to send.
    h.rapi.ClearRecording();
    Run(DisplayList(G_TD_DETAIL, G_CYC_2CYCLE, true));
    EXPECT_TRUE(h.rapi.primLodMinCalls.empty());
}

TEST_F(TextureDetailTest, SharpenComputesLodFractionOverTheChain) {
    Run(DisplayList(G_TD_SHARPEN, G_CYC_2CYCLE, false));

    CCFeatures features = DrawnShaderFeatures();
    EXPECT_TRUE(features.opt_lod_sharpen);
    EXPECT_FALSE(features.opt_lod_detail);
    EXPECT_EQ(features.c[0][0][2], SHADER_LOD_FRACTION);

    auto* slot0 = h.interpreter->mRenderingState.mTextures[0];
    auto* slot1 = h.interpreter->mRenderingState.mTextures[1];
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(slot0->second.texture_id, slot1->second.texture_id);
    EXPECT_EQ(slot0->first.mip_levels, 4);
}

TEST_F(TextureDetailTest, SharpenNeedsASecondLevel) {
    Run(DisplayList(G_TD_SHARPEN, G_CYC_2CYCLE, false, /*maxLevel=*/0));

    CCFeatures features = DrawnShaderFeatures();
    EXPECT_FALSE(features.opt_lod_sharpen);
    EXPECT_NE(features.c[0][0][2], SHADER_LOD_FRACTION);
}

TEST_F(TextureDetailTest, OneCycleModeHasNoLod) {
    Run(DisplayList(G_TD_DETAIL, G_CYC_1CYCLE, true));

    CCFeatures features = DrawnShaderFeatures();
    EXPECT_FALSE(features.opt_lod_detail);
    EXPECT_TRUE(h.rapi.primLodMinCalls.empty());
}

TEST_F(TextureDetailTest, ClampModeKeepsLodFractionAsAVertexInput) {
    Run(DisplayList(G_TD_CLAMP, G_CYC_2CYCLE, false));

    CCFeatures features = DrawnShaderFeatures();
    EXPECT_FALSE(features.opt_lod_detail);
    EXPECT_FALSE(features.opt_lod_sharpen);
    EXPECT_GE(features.c[0][0][2], SHADER_INPUT_1);
    EXPECT_LE(features.c[0][0][2], SHADER_INPUT_7);
}

TEST(ShaderIdLayout, OptionBitsStayBelowTheShaderId) {
    EXPECT_LT(SHADER_OPT(TEX_SHARPEN), (uint64_t)1 << SHADER_ID_SHIFT);
    const uint64_t withId = SHADER_OPT(TEX_DETAIL) | ((uint64_t)0x1234 << SHADER_ID_SHIFT);
    EXPECT_EQ(ShaderIdUnmask(withId), 0x1234);
    // No shader pushed: every bit from SHADER_ID_SHIFT up is set, which reads back as -1.
    EXPECT_EQ(ShaderIdUnmask(~(((uint64_t)1 << SHADER_ID_SHIFT) - 1) | SHADER_OPT(TEX_DETAIL)), -1);
}

} // namespace
} // namespace Fast
