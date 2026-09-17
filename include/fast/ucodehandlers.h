#pragma once

typedef enum UcodeHandlers {
    ucode_f3db,
    ucode_f3d,
    ucode_f3dex,
    ucode_f3dexb,
    ucode_f3dex2,
    ucode_s2dex,
    // Rare's "Indy" engine microcode, shared by GoldenEye 007 and Perfect Dark - narrowly diverges
    // from F3DEX2 on the SP/vertex side (G_TRI4, GE's G_SETTEX, PD's G_COL). Split into two
    // entries, not one, because GE's and PD's G_VTX are NOT interchangeable: GE's Vtx is
    // byte-identical to the standard 16-byte F3DEX2 layout, but PD's is its own 12-byte struct
    // (x,y,z,flags,colour-index,s,t) with per-vertex colour resolved through G_COL's table
    // instead of carried inline. See issue #28 and fast/indy.h.
    ucode_indy_ge,
    ucode_indy_pd,
    ucode_max,
} UcodeHandlers;
