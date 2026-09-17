#pragma once

// Opcode surface for Rare's "Indy" engine RSP microcode, shared by GoldenEye 007 and
// Perfect Dark. See issue #28 for the design/LoE writeup this was scoped from.
//
// Confirmed against both PC ports' own port/fast3d/gfx_pc.cpp (goldeneye-pc-port,
// perfect-dark-pc-port/perfect_dark) - the proven, hardware-validated dispatch logic issue #28
// says to adapt from, not the decomps' own gbi_extension.h/gbiex.h headers. Not yet audited
// against either game's actual RSP assembly dispatch table (rsp/graphics/gmain.s). Everything
// else this ucode dispatches through is inherited from F3DEX2Handlers as an unaudited baseline;
// only these opcodes (plus G_VTX, see below) are confirmed to diverge:
//   - G_TRI4    (0xb1): shared by both games, byte-identical gfx_sp_tri4() in both ports'
//                        gfx_pc.cpp. Ported below - see DecodeIndyTri4Vertices().
//   - G_SETTEX  (0xc0): GE-only, custom texture-bank selection (gsSPUseTexture).
//                        goldeneye-pc-port's own gfx_pc.cpp treats it as a no-op, with the
//                        comment that real GE game data never emits it - implemented as a
//                        no-op here for the same reason, not stubbed out of caution.
//   - G_COL     (0x07): PD-only, vertex-colour-table DMA (gsSPVertexColors). Replaces F3DEX2's
//                        G_QUAD at the same opcode slot; the Indy ucode does not expose
//                        G_QUAD. Confirmed decode: count = w0 bits[0,16)/4 (unused - PD's own
//                        gfx_pc.cpp stores the table pointer only, never checks count), table =
//                        seg_addr(w1). Implemented (see gfx_col_handler_indy_pd).
//
// G_VTX also diverges, which is why this is two ucode_handlers entries (ucode_indy_ge,
// ucode_indy_pd) rather than one: GE's Vtx (include/PR/gbi.h in goldeneye-pc-port) is
// byte-identical to F3DEX2's standard 16-byte layout, so ucode_indy_ge reuses
// gfx_vtx_handler_f3dex2 unchanged. PD's Vtx (include/PR/gbi.h in perfect_dark) is its own
// 12-byte struct - {s16 x,y,z; u8 flags; u8 colour; s16 s,t} - with no inline colour/normal at
// all; PD's own gfx_sp_vertex() resolves colour by indexing G_COL's table with `colour >> 2`.
// ucode_indy_pd gets its own gfx_vtx_handler_indy_pd() that decodes this layout and adapts it
// into the existing GfxSpVertex() pipeline instead of duplicating its lighting/fog/clip logic.
//
// This adapted dispatch logic originates in Emill & MaikelChan's MIT-licensed `fast3d` project
// (2020) - see port/fast3d/LICENSE.txt in either PC port repo, both copyright Emill/MaikelChan
// 2020 verbatim. Credit goldeneye-pc-port (Dansereau) and perfect_dark (Dwyer) separately for
// their own port/fast3d/gfx_pc.cpp adaptations of it, per issue #28. Only bare opcode/struct
// layout values (not prose or comments) are drawn from either decomp's own (unlicensed)
// gbi_extension.h / gbiex.h headers.
constexpr int8_t INDY_G_COL = OPCODE(0x07);
constexpr int8_t INDY_G_TRI4 = OPCODE(0xb1);
constexpr int8_t INDY_G_SETTEX = OPCODE(0xc0);
