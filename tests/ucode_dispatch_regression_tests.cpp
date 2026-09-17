#include <gtest/gtest.h>
#include <string>

#include "fast/interpreter.h"

// Regression coverage for issue #28's "must provably not change f3dex2/s2dex behaviour" gate,
// plus the "explicit divergence coverage" test-coverage item for the new Indy-engine ucodes
// (ucode_indy_ge/ucode_indy_pd). GfxGetOpcodeName()/gfx_set_target_ucode() are plain free
// functions over static dispatch-table state - no Interpreter/GfxRenderingAPI instance needed,
// so this is real desktop coverage of the dispatch *wiring* (which opcode resolves to which
// handler), not of handler behavior (which does need a live Interpreter - see
// liz-baker/lus-wiiu-harness#18).

namespace Fast {
namespace {

class UcodeDispatchTest : public testing::Test {
  protected:
    void TearDown() override {
        // GfxGetOpcodeName()'s ucode selection is global state; leave it at the documented
        // default so this file's tests don't affect any that run after them.
        gfx_set_target_ucode(ucode_f3dex2);
    }

    static std::string NameOf(UcodeHandlers ucode, int8_t opcode) {
        gfx_set_target_ucode(ucode);
        const char* name = GfxGetOpcodeName(opcode);
        return name == nullptr ? std::string() : std::string(name);
    }
};

// ---- f3dex2 regression snapshot ----
// None of this PR's changes touch f3dex2Handlers - this snapshot exists to prove that, not to
// re-derive it. A failure here means something unrelated shifted this table's opcode wiring.

TEST_F(UcodeDispatchTest, F3dex2OpcodesUnchanged) {
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_NOOP), "G_NOOP");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_SPNOOP), "G_SPNOOP");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_CULLDL), "G_CULLDL");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_MTX), "G_MTX");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_POPMTX), "G_POPMTX");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_MOVEMEM), "G_MOVEMEM");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_MOVEWORD), "G_MOVEWORD");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_TEXTURE), "G_TEXTURE");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_VTX), "G_VTX");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_MODIFYVTX), "G_MODIFYVTX");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_DL), "G_DL");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_ENDDL), "G_ENDDL");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_GEOMETRYMODE), "G_GEOMETRYMODE");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_TRI1), "G_TRI1");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_TRI2), "G_TRI2");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_QUAD), "G_QUAD");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_SETOTHERMODE_L), "G_SETOTHERMODE_L");
    EXPECT_EQ(NameOf(ucode_f3dex2, F3DEX2_G_SETOTHERMODE_H), "G_SETOTHERMODE_H");
}

// ---- s2dex regression snapshot ----

TEST_F(UcodeDispatchTest, S2dexOpcodesUnchanged) {
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_BG_COPY), "G_BG_COPY");
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_BG_1CYC), "G_BG_1CYC");
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_OBJ_RENDERMODE), "G_OBJ_RENDERMODE");
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_OBJ_RECTANGLE_R), "G_OBJ_RECTANGLE_R");
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_OBJ_RECTANGLE), "G_OBJ_RECTANGLE");
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_DL), "G_DL");
    EXPECT_EQ(NameOf(ucode_s2dex, F3DEX2_G_ENDDL), "G_ENDDL");
}

// ---- Indy engine (GE/PD) divergence coverage ----
// From issue #28's test-coverage plan: GE must handle G_SETTEX and not misfire on G_COL (which
// GE's ucode never emits); PD must handle G_COL and not misfire on G_SETTEX (which PD's ucode
// never emits). "Not misfire" means the opcode slot literally isn't wired for that variant, not
// just "the handler happens to no-op."

TEST_F(UcodeDispatchTest, GoldenEyeHasSettexNotCol) {
    EXPECT_EQ(NameOf(ucode_indy_ge, INDY_G_SETTEX), "G_SETTEX");
    // INDY_G_COL (0x07) is F3DEX_G_RESERVED2 in stock F3DEX - genuinely unhandled for GE, not a
    // handler that happens to do nothing.
    EXPECT_EQ(NameOf(ucode_indy_ge, INDY_G_COL), "");
}

TEST_F(UcodeDispatchTest, PerfectDarkHasColNotSettex) {
    EXPECT_EQ(NameOf(ucode_indy_pd, INDY_G_COL), "G_COL");
    // INDY_G_SETTEX (0xc0) is F3DEX_G_NOOP's slot in stock F3DEX - PD wires it as an ordinary
    // noop, not as G_SETTEX (which is a GE-only extension PD's ucode never defined).
    EXPECT_EQ(NameOf(ucode_indy_pd, INDY_G_SETTEX), "G_NOOP");
}

TEST_F(UcodeDispatchTest, GTri4SharedByBothIndyVariants) {
    EXPECT_EQ(NameOf(ucode_indy_ge, INDY_G_TRI4), "G_TRI4");
    EXPECT_EQ(NameOf(ucode_indy_pd, INDY_G_TRI4), "G_TRI4");
}

TEST_F(UcodeDispatchTest, IndyVariantsDoNotWireRemovedOpcodes) {
    // Confirmed dead in perfect_dark's src/rsp/gsp.s ("imm_popmtx removed in PD" /
    // "imm_culldl removed in PD") and absent from GE's own gbi.h with F3DEX_GBI off - see
    // fast/indy.h. Neither variant should have these wired at all.
    EXPECT_EQ(NameOf(ucode_indy_ge, F3DEX_G_POPMTX), "");
    EXPECT_EQ(NameOf(ucode_indy_ge, F3DEX_G_CULLDL), "");
    EXPECT_EQ(NameOf(ucode_indy_pd, F3DEX_G_POPMTX), "");
    EXPECT_EQ(NameOf(ucode_indy_pd, F3DEX_G_CULLDL), "");
}

} // namespace
} // namespace Fast
