#pragma once

typedef enum UcodeHandlers {
    ucode_f3db,
    ucode_f3d,
    ucode_f3dex,
    ucode_f3dexb,
    ucode_f3dex2,
    ucode_s2dex,
    // Rare's "Indy" engine microcode, shared by GoldenEye 007 and Perfect Dark - narrowly diverges
    // from F3DEX2 on the SP/vertex side only (G_TRI4, GE's G_SETTEX, PD's G_COL). See issue #28.
    ucode_indy,
    ucode_max,
} UcodeHandlers;
