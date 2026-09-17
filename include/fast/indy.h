#pragma once

// Opcode surface for Rare's "Indy" engine RSP microcode, shared by GoldenEye 007 and
// Perfect Dark. See issue #28 for the design/LoE writeup this was scoped from.
//
// Confirmed by direct comparison of GE's include/gbi_extension.h (n64decomp/007) against
// PD's src/include/gbiex.h (perfect-dark-pc-port/perfect_dark) - not yet audited against
// either game's actual RSP assembly dispatch table (rsp/graphics/gmain.s). Everything else
// this ucode dispatches through is inherited from F3DEX2Handlers as an unaudited baseline;
// only these three opcodes are confirmed to diverge:
//   - G_TRI4    (0xb1): shared by both games, identical opcode and 4-bit-packed
//                        vertex-index encoding in both headers.
//   - G_SETTEX  (0xc0): GE-only, custom texture-bank selection. Not implemented yet -
//                        Fast3D has no texture-bank concept to hang this off of.
//   - G_COL     (0x07): PD-only, vertex-colour-table DMA. Replaces F3DEX2's G_QUAD at
//                        the same opcode slot in this table; the Indy ucode does not
//                        expose G_QUAD. Not implemented yet - needs a vertex-colour-table
//                        store this interpreter doesn't have.
//
// G_TRI4's decode here is an independent implementation of the documented shared semantics
// (the classic gsSP1Triangle4(v0..v11, flag) SDK macro shape, plus the "all-zero vertex indices
// = padding, not drawn" rule both GE's and PD's decomp headers call out) - it was NOT ported
// from goldeneye-pc-port's or perfect_dark's port/fast3d/gfx_pc.cpp, which weren't reachable
// from this session. Issue #28's guidance still applies to whoever implements G_SETTEX/G_COL
// (and re-verifies G_TRI4) against that actual, hardware-validated source: attribute the
// adapted dispatch logic to Emill & MaikelChan's MIT-licensed `fast3d` project (2020) as its
// true origin, credit the two PC ports' own adaptations separately, and do not copy prose or
// comments from either decomp's (unlicensed) gbi_extension.h/gbiex.h headers - only their bare
// opcode values are used here.
constexpr int8_t INDY_G_COL = OPCODE(0x07);
constexpr int8_t INDY_G_TRI4 = OPCODE(0xb1);
constexpr int8_t INDY_G_SETTEX = OPCODE(0xc0);
