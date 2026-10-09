#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
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

// A 4-bit chain of odd size loaded the way a GoldenEye texture request loads it: one block, no dxt,
// as bytes, with the base level's padded row named as the image width. The block is not a whole
// number of base rows, and each level past the base has a row stride of its own.
class FourBitChainDrawTest : public testing::Test {
  protected:
    static constexpr uint32_t kBaseSize = 65;
    static constexpr uint32_t kLevels = 4;

    struct Level {
        uint32_t width, height, tmemWords, lineWords;
    };

    void SetUp() override {
        uint32_t size = kBaseSize;
        uint32_t tmem = 0;
        for (uint32_t n = 0; n < kLevels; n++) {
            const uint32_t lineWords = (size + 15) / 16;
            levels.push_back({ size, size, tmem, lineWords });
            tmem += lineWords * size;
            size = (size + 1) / 2;
        }
        texels.assign(tmem * 8, 0);
        for (uint32_t n = 0; n < kLevels; n++) {
            // A different nibble in every level, so a level read from the wrong rows shows.
            const uint8_t nibble = (uint8_t)(n + 1);
            for (uint32_t y = 0; y < levels[n].height; y++) {
                for (uint32_t x = 0; x < levels[n].width; x++) {
                    uint8_t& byte = texels[levels[n].tmemWords * 8 + y * levels[n].lineWords * 8 + x / 2];
                    byte |= (x % 2 == 0) ? (nibble << 4) : nibble;
                }
            }
        }
        for (int i = 0; i < 3; i++) {
            vtx[i].v.cn[3] = 255;
        }
        vtx[1].v.ob[0] = 1;
        vtx[2].v.ob[1] = 1;
    }

    void Run() {
        std::vector<Gfx> dl = {
            gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
            gsDPSetCycleType(G_CYC_2CYCLE),
            gsDPSetTextureLOD(G_TL_TILE),
            gsDPSetCombineMode(G_CC_TRILERP, G_CC_TRILERP),
            gsSPTexture(0xFFFF, 0xFFFF, kLevels - 1, G_TX_RENDERTILE, G_ON),
            gsDPSetTextureImage(G_IM_FMT_I, G_IM_SIZ_8b, levels[0].lineWords * 8, texels.data()),
            gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_8b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0),
            gsDPLoadSync(),
            gsDPLoadBlock(G_TX_LOADTILE, 0, 0, (uint32_t)texels.size() - 1, 0),
            gsDPPipeSync(),
        };
        for (uint8_t n = 0; n < kLevels; n++) {
            const Level& level = levels[n];
            dl.push_back(gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_4b, level.lineWords, level.tmemWords, n, 0, G_TX_CLAMP, 0, n,
                                     G_TX_CLAMP, 0, n));
            dl.push_back(gsDPSetTileSize(n, 0, 0, (level.width - 1) << G_TEXTURE_IMAGE_FRAC,
                                         (level.height - 1) << G_TEXTURE_IMAGE_FRAC));
        }
        dl.push_back(gsSPVertex(vtx, 3, 0));
        dl.push_back(gsSP1Triangle(0, 1, 2, 0));
        dl.push_back(gsSPEndDisplayList());
        h.Run(dl.data());
    }

    static void ExpectIntensity(const NullGfxRenderingAPI::Upload& up, uint32_t size, uint8_t nibble) {
        ASSERT_EQ(up.width, size);
        ASSERT_EQ(up.height, size);
        const uint8_t expected = (uint8_t)(nibble * 17);
        for (size_t p = 0; p < (size_t)size * size; p++) {
            ASSERT_EQ(up.rgba32[p * 4], expected) << "texel " << p;
        }
    }

    NullBackendInterpreter h;
    FixedMtx identity = IdentityMtx();
    Vtx vtx[3] = {};
    std::vector<Level> levels;
    std::vector<uint8_t> texels;
};

// Without texture LOD the draw samples tile 0 and, for TEXEL1, tile 1: each must read its own rows.
TEST_F(FourBitChainDrawTest, EachTileReadsItsOwnRows) {
    Run();

    ASSERT_EQ(h.rapi.uploads.size(), 2u);
    ExpectIntensity(h.rapi.uploads[0], levels[0].width, 1);
    ExpectIntensity(h.rapi.uploads[1], levels[1].width, 2);
}

// A paletted 8-bit chain whose base level is 33 texels wide, so its rows are padded to 40 bytes: the
// uploaded base level is the same image as the same texels loaded with compact rows.
class Ci8ChainDrawTest : public testing::Test {
  protected:
    static constexpr uint32_t kSize = 33;
    static constexpr uint32_t kPaddedRow = 40;
    static constexpr uint32_t kLevel1Size = 17;
    static constexpr uint32_t kLevel1Row = 24;
    static constexpr uint32_t kLevel1Tmem = kPaddedRow / 8 * kSize;

    static uint8_t Texel(uint32_t x, uint32_t y) {
        return (uint8_t)(x * 3 + y * 5);
    }

    void SetUp() override {
        for (uint32_t i = 0; i < 256; i++) {
            const uint16_t color = (uint16_t)(((i & 31) << 11) | (((i * 7) & 31) << 6) | (((i * 3) & 31) << 1) | 1);
            palette[i * 2] = color >> 8;
            palette[i * 2 + 1] = color & 0xFF;
        }
        compact.resize(kSize * kSize);
        chain.assign(kPaddedRow * kSize + kLevel1Row * kLevel1Size, 0);
        for (uint32_t y = 0; y < kSize; y++) {
            for (uint32_t x = 0; x < kSize; x++) {
                compact[y * kSize + x] = Texel(x, y);
                chain[y * kPaddedRow + x] = Texel(x, y);
            }
        }
        for (uint32_t i = 0; i < kLevel1Row * kLevel1Size; i++) {
            chain[kPaddedRow * kSize + i] = 0xAA;
        }
        for (int i = 0; i < 3; i++) {
            vtx[i].v.cn[3] = 255;
        }
        vtx[1].v.ob[0] = 1;
        vtx[2].v.ob[1] = 1;
    }

    std::vector<Gfx> Draw(std::vector<Gfx> load) {
        std::vector<Gfx> dl = {
            gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
            gsDPSetCycleType(G_CYC_1CYCLE),
            gsDPSetTextureLUT(G_TT_RGBA16),
            gsDPSetCombineMode(G_CC_DECALRGB, G_CC_DECALRGB),
            gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
            gsDPLoadTLUT_pal256(palette.data()),
        };
        dl.insert(dl.end(), load.begin(), load.end());
        dl.push_back(gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, kPaddedRow / 8, 0, 0, 0, G_TX_CLAMP, 6, 0, G_TX_CLAMP, 6, 0));
        dl.push_back(gsDPSetTileSize(0, 0, 0, (kSize - 1) << G_TEXTURE_IMAGE_FRAC, (kSize - 1) << G_TEXTURE_IMAGE_FRAC));
        dl.push_back(gsSPVertex(vtx, 3, 0));
        dl.push_back(gsSP1Triangle(0, 1, 2, 0));
        dl.push_back(gsSPEndDisplayList());
        return dl;
    }

    // Runs `dl` on a fresh interpreter: only one can be alive at a time.
    static std::vector<NullGfxRenderingAPI::Upload> Uploads(std::vector<Gfx> dl) {
        auto harness = std::make_unique<NullBackendInterpreter>();
        harness->Run(dl.data());
        return harness->rapi.uploads;
    }

    FixedMtx identity = IdentityMtx();
    Vtx vtx[3] = {};
    std::array<uint8_t, 512> palette{};
    std::vector<uint8_t> compact;
    std::vector<uint8_t> chain;
};

TEST_F(Ci8ChainDrawTest, PaddedBaseLevelUploadsLikeCompactRows) {
    // The image as the game loads a single level: compact rows.
    std::vector<Gfx> compactLoad = {
        gsDPSetTextureImage(G_IM_FMT_CI, G_IM_SIZ_8b, kSize, compact.data()),
        gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0),
        gsDPLoadSync(),
        gsDPLoadBlock(G_TX_LOADTILE, 0, 0, kSize * kSize - 1, 410),
        gsDPPipeSync(),
    };
    const auto reference = Uploads(Draw(compactLoad));
    ASSERT_EQ(reference.size(), 1u);

    // The same texels as a chain: one block, loaded as 16-bit texels with no dxt, the base level's
    // padded row as the image width and a second level after it.
    std::vector<Gfx> chainLoad = {
        gsDPSetTextureImage(G_IM_FMT_CI, G_IM_SIZ_16b, kPaddedRow / 2, chain.data()),
        gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0),
        gsDPLoadSync(),
        gsDPLoadBlock(G_TX_LOADTILE, 0, 0, (uint32_t)chain.size() / 2 - 1, 0),
        gsDPPipeSync(),
        gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, kLevel1Row / 8, kLevel1Tmem, 1, 0, G_TX_CLAMP, 5, 1, G_TX_CLAMP, 5, 1),
        gsDPSetTileSize(1, 0, 0, (kLevel1Size - 1) << G_TEXTURE_IMAGE_FRAC, (kLevel1Size - 1) << G_TEXTURE_IMAGE_FRAC),
    };
    const auto uploads = Uploads(Draw(chainLoad));
    ASSERT_EQ(uploads.size(), 1u);

    const auto& want = reference[0];
    const auto& got = uploads[0];
    ASSERT_EQ(got.width, want.width);
    ASSERT_EQ(got.height, want.height);
    EXPECT_EQ(got.rgba32, want.rgba32);
}

// A 64x64 4-bit chain on a tile that wraps (a mask of 6) rather than clamps: the block holds the lower
// levels below the base level, and the upload is cut back to the tile like the draw sizes its texture.
TEST(WrappedFourBitChainDrawTest, UploadIsCutBackToTheMaskedTile) {
    constexpr uint32_t kRowBytes = 32;
    // 64x64, 32x32, 16x16 and 8x8 at rows of 32, 16, 8 and 8 bytes.
    constexpr uint32_t kChainBytes = 2048 + 512 + 128 + 64;
    std::vector<uint8_t> texels(kChainBytes, 0x11);
    FixedMtx identity = IdentityMtx();
    Vtx vtx[3] = {};
    for (int i = 0; i < 3; i++) {
        vtx[i].v.cn[3] = 255;
    }
    vtx[1].v.ob[0] = 1;
    vtx[2].v.ob[1] = 1;

    std::vector<Gfx> dl = {
        gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
        gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
        gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
        gsDPSetCycleType(G_CYC_1CYCLE),
        gsDPSetCombineMode(G_CC_DECALRGB, G_CC_DECALRGB),
        gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
        gsDPSetTextureImage(G_IM_FMT_I, G_IM_SIZ_16b, kRowBytes / 2, texels.data()),
        gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0),
        gsDPLoadSync(),
        gsDPLoadBlock(G_TX_LOADTILE, 0, 0, kChainBytes / 2 - 1, 0),
        gsDPPipeSync(),
        gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_4b, kRowBytes / 8, 0, 0, 0, G_TX_WRAP, 6, 0, G_TX_WRAP, 6, 0),
        gsDPSetTileSize(0, 0, 0, 63 << G_TEXTURE_IMAGE_FRAC, 63 << G_TEXTURE_IMAGE_FRAC),
        gsSPVertex(vtx, 3, 0),
        gsSP1Triangle(0, 1, 2, 0),
        gsSPEndDisplayList(),
    };
    NullBackendInterpreter h;
    h.Run(dl.data());

    ASSERT_EQ(h.rapi.uploads.size(), 1u);
    const auto& up = h.rapi.uploads[0];
    EXPECT_EQ(up.width, 64u);
    EXPECT_EQ(up.height, 64u);
}

// 8-bit and 16-bit I and IA chains on a tile that wraps: the block holds the lower levels below the
// base level, and the upload is cut back to the tile (a 64x32 chain of 2720 bytes is 42 rows of 64).
TEST(WrappedByteChainDrawTest, UploadIsCutBackToTheMaskedTile) {
    struct Case {
        uint32_t fmt, siz, width, height, masks, maskt;
        const char* name;
    };
    const Case cases[] = {
        { G_IM_FMT_I, G_IM_SIZ_8b, 64, 32, 6, 5, "I8" },
        { G_IM_FMT_IA, G_IM_SIZ_8b, 64, 32, 6, 5, "IA8" },
        { G_IM_FMT_IA, G_IM_SIZ_16b, 32, 32, 5, 5, "IA16" },
    };
    // Four levels at rows of 64, 32, 16 and 8 bytes: 2048 + 512 + 128 + 32 bytes.
    constexpr uint32_t kChainBytes = 2720;
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        const uint32_t rowBytes = c.width * (c.siz == G_IM_SIZ_16b ? 2 : 1);
        std::vector<uint8_t> texels(kChainBytes, 0x11);
        FixedMtx identity = IdentityMtx();
        Vtx vtx[3] = {};
        for (int i = 0; i < 3; i++) {
            vtx[i].v.cn[3] = 255;
        }
        vtx[1].v.ob[0] = 1;
        vtx[2].v.ob[1] = 1;

        std::vector<Gfx> dl = {
            gsSPMatrix(&identity, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPMatrix(&identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
            gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG),
            gsDPSetCycleType(G_CYC_1CYCLE),
            gsDPSetCombineMode(G_CC_DECALRGB, G_CC_DECALRGB),
            gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
            gsDPSetTextureImage(c.fmt, G_IM_SIZ_16b, rowBytes / 2, texels.data()),
            gsDPSetTile(c.fmt, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0),
            gsDPLoadSync(),
            gsDPLoadBlock(G_TX_LOADTILE, 0, 0, kChainBytes / 2 - 1, 0),
            gsDPPipeSync(),
            gsDPSetTile(c.fmt, c.siz, rowBytes / 8, 0, 0, 0, G_TX_WRAP, c.maskt, 0, G_TX_WRAP, c.masks, 0),
            gsDPSetTileSize(0, 0, 0, (c.width - 1) << G_TEXTURE_IMAGE_FRAC, (c.height - 1) << G_TEXTURE_IMAGE_FRAC),
            gsSPVertex(vtx, 3, 0),
            gsSP1Triangle(0, 1, 2, 0),
            gsSPEndDisplayList(),
        };
        NullBackendInterpreter h;
        h.Run(dl.data());

        ASSERT_EQ(h.rapi.uploads.size(), 1u);
        EXPECT_EQ(h.rapi.uploads[0].width, c.width);
        EXPECT_EQ(h.rapi.uploads[0].height, c.height);
    }
}

} // namespace
} // namespace Fast
