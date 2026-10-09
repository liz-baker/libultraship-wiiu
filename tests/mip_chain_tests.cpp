#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

// Encode with the F3DEX2 GBI, which is the interpreter's default ucode.
#define F3DEX_GBI_2
#include "libultraship/libultra/gbi.h"
#include "support/null_gfx_backend.h"

// Issue #77: G_TL_LOD mip chains are uploaded as one mipmapped texture.

namespace Fast {
namespace {

// ---- ResolveMipChain over hand-built RDP state ----

// RGBA16 levels of an 8x8 chain, each loaded at its own TMEM offset from one block: 8x8 (16 words),
// 4x4 (4 rows of one word), 2x2 and 1x1 (rows padded to a word).
struct ChainLayout {
    uint32_t width, height, tmemWords, lineWords;
};
constexpr std::array<ChainLayout, 4> kRgba16Chain = { {
    { 8, 8, 0, 2 },
    { 4, 4, 16, 1 },
    { 2, 2, 20, 1 },
    { 1, 1, 22, 1 },
} };
constexpr uint32_t kRgba16ChainWords = 23;

void SetTileSizeTexels(RDP& rdp, uint8_t tile, uint32_t width, uint32_t height) {
    rdp.texture_tile[tile].uls = 0;
    rdp.texture_tile[tile].ult = 0;
    rdp.texture_tile[tile].lrs = (float)((width - 1) * 4);
    rdp.texture_tile[tile].lrt = (float)((height - 1) * 4);
}

class ResolveMipChainTest : public testing::Test {
  protected:
    void SetUp() override {
        rdp.other_mode_h = G_CYC_2CYCLE | G_TL_LOD | G_TD_CLAMP;
        rdp.loaded_texture[0].addr = block.data();
        rdp.loaded_texture[0].size_bytes = kRgba16ChainWords * 8;
        rdp.loaded_texture[0].tmem_base = 0;
        for (uint8_t n = 0; n < kRgba16Chain.size(); n++) {
            auto& tile = rdp.texture_tile[n];
            tile.fmt = G_IM_FMT_RGBA;
            tile.siz = G_IM_SIZ_16b;
            tile.tmem = kRgba16Chain[n].tmemWords;
            tile.line_size_bytes = kRgba16Chain[n].lineWords * 8;
            tile.tmem_index = 0;
            SetTileSizeTexels(rdp, n, kRgba16Chain[n].width, kRgba16Chain[n].height);
        }
    }

    RDP rdp{};
    std::array<uint8_t, kRgba16ChainWords * 8> block{};
};

TEST_F(ResolveMipChainTest, CollectsEveryHalvingLevel) {
    MipChain chain = ResolveMipChain(rdp, 0, 3);
    ASSERT_EQ(chain.numLevels, 4);
    for (uint8_t n = 0; n < 4; n++) {
        SCOPED_TRACE(n);
        EXPECT_EQ(chain.levels[n].tile, n);
        EXPECT_EQ(chain.levels[n].width, kRgba16Chain[n].width);
        EXPECT_EQ(chain.levels[n].height, kRgba16Chain[n].height);
    }
}

TEST_F(ResolveMipChainTest, StopsAtMaxLevel) {
    EXPECT_EQ(ResolveMipChain(rdp, 0, 1).numLevels, 2);
}

TEST_F(ResolveMipChainTest, NoChainWithoutLevels) {
    EXPECT_EQ(ResolveMipChain(rdp, 0, 0).numLevels, 0);
}

TEST_F(ResolveMipChainTest, NoChainWithoutTextureLod) {
    rdp.other_mode_h &= ~G_TL_LOD;
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 0);
}

TEST_F(ResolveMipChainTest, NoChainInOneCycleMode) {
    rdp.other_mode_h = (rdp.other_mode_h & ~(3U << G_MDSFT_CYCLETYPE)) | G_CYC_1CYCLE;
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 0);
}

TEST_F(ResolveMipChainTest, DetailModeLeavesTheBaseTileToTheCaller) {
    // In G_TD_DETAIL mode the interpreter passes the tile after the detail tile as the base.
    rdp.other_mode_h |= G_TD_DETAIL;
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 4);
}

TEST_F(ResolveMipChainTest, TruncatesAtALevelThatDoesNotHalve) {
    SetTileSizeTexels(rdp, 2, 3, 2);
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 2);
}

TEST_F(ResolveMipChainTest, NoChainWhenTheFirstLowerLevelDoesNotHalve) {
    // Some display lists point tile 1 at the same image to get a TRILERP combiner without a chain.
    rdp.texture_tile[1] = rdp.texture_tile[0];
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 0);
}

TEST_F(ResolveMipChainTest, TruncatesAtAFormatChange) {
    rdp.texture_tile[3].fmt = G_IM_FMT_IA;
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 3);
}

TEST_F(ResolveMipChainTest, TruncatesAtALevelPastTheLoadedBlock) {
    rdp.loaded_texture[0].size_bytes = 22 * 8;
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 3);
}

TEST_F(ResolveMipChainTest, StartsAtTheGivenBaseTile) {
    // Shift the whole chain up to tiles 2..5.
    for (int n = 3; n >= 0; n--) {
        rdp.texture_tile[n + 2] = rdp.texture_tile[n];
    }
    MipChain chain = ResolveMipChain(rdp, 2, 3);
    ASSERT_EQ(chain.numLevels, 4);
    EXPECT_EQ(chain.levels[0].tile, 2);
    EXPECT_EQ(chain.levels[3].tile, 5);
}

TEST_F(ResolveMipChainTest, Rgba32IsNotChained) {
    for (uint8_t n = 0; n < 4; n++) {
        rdp.texture_tile[n].siz = G_IM_SIZ_32b;
    }
    EXPECT_EQ(ResolveMipChain(rdp, 0, 3).numLevels, 0);
}

TEST(TextureLodBiasForScale, MatchesNativeLevelPick) {
    EXPECT_FLOAT_EQ(TextureLodBiasForScale(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(TextureLodBiasForScale(2.0f), 1.0f);
    EXPECT_FLOAT_EQ(TextureLodBiasForScale(4.0f), 2.0f);
    EXPECT_FLOAT_EQ(TextureLodBiasForScale(3.0f), std::log2(3.0f));
}

TEST(TextureLodBiasForScale, NeverSharpensBelowNative) {
    EXPECT_FLOAT_EQ(TextureLodBiasForScale(0.5f), 0.0f);
    EXPECT_FLOAT_EQ(TextureLodBiasForScale(0.0f), 0.0f);
}

// ---- Through Interpreter::Run() on the null backend ----

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

// RGBA5551 colours, one per level, so each uploaded level is recognisable.
constexpr std::array<uint16_t, 4> kLevelColors = { 0xF801, 0x07C1, 0x003F, 0xFFFF };
constexpr std::array<std::array<uint8_t, 4>, 4> kLevelRgba = { {
    { 255, 0, 0, 255 },
    { 0, 255, 0, 255 },
    { 0, 0, 255, 255 },
    { 255, 255, 255, 255 },
} };

class MipChainDrawTest : public testing::Test {
  protected:
    void SetUp() override {
        // Lay each level out at its TMEM word offset, rows padded to the tile line.
        for (size_t n = 0; n < kRgba16Chain.size(); n++) {
            const auto& level = kRgba16Chain[n];
            for (uint32_t y = 0; y < level.height; y++) {
                for (uint32_t x = 0; x < level.width; x++) {
                    size_t byte = level.tmemWords * 8 + y * level.lineWords * 8 + x * 2;
                    texels[byte] = kLevelColors[n] >> 8;
                    texels[byte + 1] = kLevelColors[n] & 0xFF;
                }
            }
        }
        const short pos[3][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
        const short st[3][2] = { { 0, 0 }, { 8 << 5, 0 }, { 0, 8 << 5 } };
        for (int i = 0; i < 3; i++) {
            vtx[i].v.ob[0] = pos[i][0];
            vtx[i].v.ob[1] = pos[i][1];
            vtx[i].v.tc[0] = st[i][0];
            vtx[i].v.tc[1] = st[i][1];
            vtx[i].v.cn[3] = 255;
        }
    }

    // Draws one TRILERP triangle with the chain loaded by a single G_LOADBLOCK, then ends the list.
    // `level1Width` lets a test break the chain at level 1.
    std::vector<Gfx> ChainDisplayList(uint32_t lodMode, uint32_t cycleType, uint8_t maxLevel,
                                      uint32_t level1Width = 4) {
        std::vector<Gfx> dl = {
            gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
            gsDPSetCycleType(cycleType),
            gsDPSetTextureLOD(lodMode),
            gsDPSetTextureDetail(G_TD_CLAMP),
            gsDPSetCombineMode(G_CC_TRILERP, G_CC_TRILERP),
            gsSPTexture(0xFFFF, 0xFFFF, maxLevel, G_TX_RENDERTILE, G_ON),
            gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texels.data()),
            gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0),
            gsDPLoadSync(),
            gsDPLoadBlock(G_TX_LOADTILE, 0, 0, kRgba16ChainWords * 4 - 1, 0),
            gsDPPipeSync(),
        };
        for (uint8_t n = 0; n < kRgba16Chain.size(); n++) {
            const auto& level = kRgba16Chain[n];
            uint32_t width = n == 1 ? level1Width : level.width;
            dl.push_back(gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, level.lineWords, level.tmemWords, n, 0, G_TX_WRAP,
                                     3 - n, n, G_TX_WRAP, 3 - n, n));
            dl.push_back(gsDPSetTileSize(n, 0, 0, (width - 1) << G_TEXTURE_IMAGE_FRAC,
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

    static void ExpectSolid(const NullGfxRenderingAPI::Upload& up, uint32_t width, uint32_t height,
                            const std::array<uint8_t, 4>& rgba) {
        ASSERT_EQ(up.width, width);
        ASSERT_EQ(up.height, height);
        ASSERT_EQ(up.rgba32.size(), (size_t)width * height * 4);
        for (size_t p = 0; p < (size_t)width * height; p++) {
            for (int c = 0; c < 4; c++) {
                ASSERT_EQ(up.rgba32[p * 4 + c], rgba[c]) << "texel " << p << " channel " << c;
            }
        }
    }

    NullBackendInterpreter h;
    FixedMtx identity = IdentityMtx();
    Vtx vtx[3] = {};
    std::array<uint8_t, kRgba16ChainWords * 8> texels{};
};

TEST_F(MipChainDrawTest, UploadsEveryLevelAsOneTexture) {
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));

    ASSERT_EQ(h.rapi.uploads.size(), 1u);
    const auto& up = h.rapi.uploads[0];
    ExpectSolid(up, 8, 8, kLevelRgba[0]);
    ASSERT_EQ(up.lowerLevels.size(), 3u);
    for (size_t n = 1; n < 4; n++) {
        SCOPED_TRACE(n);
        ExpectSolid(up.lowerLevels[n - 1], kRgba16Chain[n].width, kRgba16Chain[n].height, kLevelRgba[n]);
    }
}

TEST_F(MipChainDrawTest, BothSlotsSampleTheChain) {
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));

    auto* slot0 = h.interpreter->mRenderingState.mTextures[0];
    auto* slot1 = h.interpreter->mRenderingState.mTextures[1];
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(slot0->second.texture_id, slot1->second.texture_id);
    EXPECT_EQ(slot0->first.mip_levels, 4);
}

TEST_F(MipChainDrawTest, SecondDrawReusesTheCachedChain) {
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));
    h.rapi.ClearRecording();
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));
    EXPECT_TRUE(h.rapi.uploads.empty());
}

TEST_F(MipChainDrawTest, WithoutTextureLodOnlyTheBaseLevelIsUploaded) {
    Run(ChainDisplayList(G_TL_TILE, G_CYC_2CYCLE, 3));

    ASSERT_FALSE(h.rapi.uploads.empty());
    for (const auto& up : h.rapi.uploads) {
        EXPECT_TRUE(up.lowerLevels.empty());
    }
}

TEST_F(MipChainDrawTest, TurningTextureLodOnReimportsTheChain) {
    Run(ChainDisplayList(G_TL_TILE, G_CYC_2CYCLE, 3));
    h.rapi.ClearRecording();
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));

    ASSERT_EQ(h.rapi.uploads.size(), 1u);
    EXPECT_EQ(h.rapi.uploads[0].lowerLevels.size(), 3u);
}

TEST_F(MipChainDrawTest, BrokenChainFallsBackToTheBaseLevel) {
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3, /*level1Width=*/3));

    ASSERT_FALSE(h.rapi.uploads.empty());
    for (const auto& up : h.rapi.uploads) {
        EXPECT_TRUE(up.lowerLevels.empty());
    }
}

TEST_F(MipChainDrawTest, LodBiasFollowsResolutionScale) {
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));
    ASSERT_FALSE(h.rapi.lodBiasCalls.empty());
    EXPECT_FLOAT_EQ(h.rapi.lodBiasCalls.front(), 0.0f);

    h.rapi.ClearRecording();
    h.interpreter->mCurDimensions.height = NullBackendInterpreter::kHeight * 4;
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));
    ASSERT_FALSE(h.rapi.lodBiasCalls.empty());
    EXPECT_FLOAT_EQ(h.rapi.lodBiasCalls.front(), 2.0f);
}

TEST_F(MipChainDrawTest, LodBiasCVarTurnsTheBiasOff) {
    h.cvars->SetInteger(CVAR_TEXTURE_LOD_NATIVE_BIAS, 0);
    h.interpreter->mCurDimensions.height = NullBackendInterpreter::kHeight * 4;
    Run(ChainDisplayList(G_TL_LOD, G_CYC_2CYCLE, 3));
    ASSERT_FALSE(h.rapi.lodBiasCalls.empty());
    for (float bias : h.rapi.lodBiasCalls) {
        EXPECT_FLOAT_EQ(bias, 0.0f);
    }
}

} // namespace
} // namespace Fast
