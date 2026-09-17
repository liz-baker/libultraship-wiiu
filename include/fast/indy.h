#pragma once

// Opcode surface for Rare's "Indy" engine RSP microcode, shared by GoldenEye 007 and
// Perfect Dark. See issue #28 for the design/LoE writeup this was scoped from.
//
// AUDIT FINDING (issue #28's "full audit against rsp/graphics/gmain.s" scope item, now
// partially resolved for PD, cross-checked for GE): this ucode's opcode numbering is NOT
// F3DEX2's, despite an earlier version of this file assuming it as an "unaudited baseline."
// It's the original F3D/F3DEX low+high range numbering (fast/f3dex.h's F3DEX_* constants) with
// Rare's own extensions layered on top:
//   - GE's own include/PR/gbi.h (goldeneye-pc-port) guards its opcode numbering behind
//     `#ifdef F3DEX_GBI_2` (F3DEX2's numbering), with an `#else` branch matching F3DEX/F3D's
//     numbering. F3DEX_GBI_2 is never defined anywhere in that port's build, so GE's ACTIVE
//     opcode numbering is the `#else` branch.
//   - perfect_dark's src/rsp/gsp.s is a symbolically labeled RSP disassembly with an explicit
//     jump table confirming this directly: `dma_dispatch_table`/`imm_dispatch_table` name every
//     opcode by its real handler (e.g. `/* cmd 04 */ .dh dma_vtx`,
//     `/* cmd b1 */ .dh imm_tri4 // new in PD`), matching F3DEX_* numbering exactly, with
//     `imm_popmtx removed in PD` / `imm_culldl removed in PD` confirming those two slots are
//     dead (also true for GE - neither's active gbi.h branch defines them either).
//   - Every reused handler function in indyGeHandlers/indyPdHandlers (interpreter.cpp) was
//     cross-checked opcode-by-opcode against both ports' port/fast3d/gfx_pc.cpp dispatch
//     switches and found to be a byte-for-byte match of the existing fast/f3dex.h-family
//     (`_f3d`-suffixed) handlers already in this file - not F3DEX2's.
//
// Rare's own extensions on top of that F3DEX/F3D base:
//   - G_TRI4    (0xb1): shared by both games, byte-identical gfx_sp_tri4() in both ports'
//                        gfx_pc.cpp. Replaces the old G_TRI2 slot (F3DEX_GBI off means neither
//                        game's ucode has G_TRI2 in the first place). Ported below - see
//                        DecodeIndyTri4Vertices() in interpreter.cpp.
//   - G_SETTEX  (0xc0): GE-only, custom texture-bank selection (gsSPUseTexture). Also F3DEX's
//                        universal G_NOOP slot - goldeneye-pc-port's own gfx_pc.cpp treats it
//                        as a no-op, with the finding that real GE game data never emits it.
//                        Implemented as a no-op on that basis, not stubbed out of caution.
//   - G_COL     (0x07): PD-only, vertex-colour-table DMA (gsSPVertexColors). Replaces
//                        F3DEX_G_RESERVED2 (unused in stock F3DEX) at the same opcode slot.
//                        Confirmed decode: count = w0 bits[0,16)/4 (unused - PD's own
//                        gfx_pc.cpp stores the table pointer only, never checks count), table =
//                        seg_addr(w1). Implemented (see gfx_col_handler_indy_pd).
//
// G_VTX also diverges between the two games, which is why this is two ucode_handlers entries
// (ucode_indy_ge, ucode_indy_pd) rather than one: GE's Vtx (include/PR/gbi.h in
// goldeneye-pc-port) is byte-identical to the standard 16-byte F3D/F3DEX layout, so
// ucode_indy_ge reuses gfx_vtx_handler_f3d unchanged. PD's Vtx (include/PR/gbi.h in
// perfect_dark) is its own 12-byte struct - {s16 x,y,z; u8 flags; u8 colour; s16 s,t} - with no
// inline colour/normal at all; PD's own gfx_sp_vertex() resolves colour by indexing G_COL's
// table with `colour >> 2`. ucode_indy_pd gets its own gfx_vtx_handler_indy_pd() that decodes
// this layout and adapts it into the existing GfxSpVertex() pipeline instead of duplicating its
// lighting/fog/clip logic.
//
// Still open: GE's own low-range custom extensions (if any) are unconfirmed beyond G_TRI4 and
// G_SETTEX - only cross-checked against PD's labeled disassembly and GE's own header, not GE's
// own equivalent of gsp.s (goldeneye-pc-port's rsp/graphics/gmain.s exists but its jump table is
// unlabeled raw data, not symbolized like PD's). GE and PD "independently extended a shared
// base in a different direction" per issue #28, so this table's F3DEX-baseline opcodes (MTX,
// MOVEMEM, DL, TRI1, TEXTURE, MOVEWORD, SETOTHERMODE_L/H, SETGEOMETRYMODE/CLEARGEOMETRYMODE)
// are confirmed for PD directly and for GE only by matching argument-decode formulas in its own
// gfx_pc.cpp - not by a symbol-level disassembly audit of GE's own microcode.
//
// This adapted dispatch logic originates in Emill & MaikelChan's MIT-licensed `fast3d` project
// (2020) - see port/fast3d/LICENSE.txt in either PC port repo, both copyright Emill/MaikelChan
// 2020 verbatim. Credit goldeneye-pc-port (Dansereau) and perfect_dark (Dwyer) separately for
// their own port/fast3d/gfx_pc.cpp adaptations of it, per issue #28. Only bare opcode/struct
// layout values (not prose or comments) are drawn from either decomp's own (unlicensed)
// gbi_extension.h / gbiex.h headers, or from perfect_dark's src/rsp/gsp.s / include/PR/gbi.h.
constexpr int8_t INDY_G_COL = OPCODE(0x07);
constexpr int8_t INDY_G_TRI4 = OPCODE(0xb1);
constexpr int8_t INDY_G_SETTEX = OPCODE(0xc0);
