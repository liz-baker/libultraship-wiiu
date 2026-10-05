#pragma once

#include "message.h"

#ifdef __WIIU__
// wut's <coreinit/time.h> declares OSTime as int64_t; a different underlying type would conflict
// in any file that includes both (e.g. through the VPAD/KPAD headers).
typedef s64 OSTime;
#else
typedef u64 OSTime;
#endif

typedef struct OSTimer {
    /* 0x00 */ struct OSTimer* next;
    /* 0x04 */ struct OSTimer* prev;
    /* 0x08 */ OSTime interval;
    /* 0x10 */ OSTime value;
    /* 0x18 */ OSMesgQueue* mq;
    /* 0x1C */ OSMesg msg;
} OSTimer; // size = 0x20
