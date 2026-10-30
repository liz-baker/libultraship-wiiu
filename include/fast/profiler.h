// Per-frame stage timers, counters and experiment switches for finding where a frame's time goes
// on a target without a profiler. Everything is compiled out unless FAST3D_PROFILE is defined, so
// a build without it carries no code and no state from here.
//
// Time is exclusive: entering a stage pauses the one below it, so the per-frame stage times add up
// to the frame. Once a second the totals are logged (the log file, and the UDP log on Wii U).
#pragma once

#include <cstdint>

namespace Fast::Prof {

// OTHER is whatever no stage claims, which for the Wii U is the game's own work between frames.
#define FAST3D_PROF_STAGES(X)                                                                          \
    X(OTHER)       /* outside every stage below: the game's tick and display-list building */          \
    X(RUN)         /* display-list command dispatch and the commands with no stage of their own */     \
    X(VTX)         /* GfxSpVertex: transform, light, clip flags */                                     \
    X(TRI_CULL)    /* GfxSpTri1 up to the color combiner: clip and cull tests, depth/viewport state */ \
    X(TRI_STATE)   /* GfxSpTri1: combiner lookup, texture size and clamp setup */                      \
    X(TRI_APPLY)   /* GfxSpTri1: texture import checks, sampler, shader and blend state */             \
    X(TRI_EMIT)    /* GfxSpTri1: writing the triangle's vertices to the buffer */                      \
    X(FLUSH)       /* Interpreter::Flush outside the backend's draw */                                 \
    X(DRAW)        /* the backend's DrawTriangles */                                                   \
    X(IMPORT)      /* ImportTexture* and the backend's texture upload */                               \
    X(GPU_SYNC)    /* the CPU waiting on the GPU (GX2DrawDone) */                                      \
    X(GUI)         /* ImGui and the final blit of the game's framebuffer */                            \
    X(SWAP)        /* SwapBuffersBegin and FinishRender */                                             \
    X(FRAME_READY) /* IsFrameReady: waiting for the previous flip */                                   \
    X(USER0)       /* named by SetUserStageName */                                                     \
    X(USER1)                                                                                           \
    X(USER2)                                                                                           \
    X(USER3)

#define FAST3D_PROF_COUNTERS(X)                                          \
    X(TRIS_IN)          /* GfxSpTri1 calls */                            \
    X(TRIS_CLIP_REJECT) /* all three vertices outside one clip plane */  \
    X(TRIS_CULL_REJECT) /* back/front face culled */                     \
    X(TRIS_EMITTED)     /* triangles written to the buffer */            \
    X(VERTS)            /* vertices GfxSpVertex loaded */                \
    X(FLUSHES)          /* Flush calls that had triangles to draw */     \
    X(FLUSH_DEPTH)      /* ... because depth test/mask changed */        \
    X(FLUSH_DECAL)      /* ... zmode decal */                            \
    X(FLUSH_VIEWPORT)   /* ... viewport or scissor */                    \
    X(FLUSH_TEXTURE)    /* ... a texture import */                       \
    X(FLUSH_SAMPLER)    /* ... sampler parameters */                     \
    X(FLUSH_SHADER)     /* ... shader program */                         \
    X(FLUSH_BLEND)      /* ... alpha blend or prim LOD */                \
    X(FLUSH_FULL)       /* ... the vertex buffer filled */               \
    X(DRAWS)            /* backend DrawTriangles calls with triangles */ \
    X(IMPORTS)          /* ImportTexture* calls */                       \
    X(UPLOAD_BYTES)     /* texture bytes handed to the backend */        \
    X(GPU_SYNCS)        /* CPU waits on the GPU */                       \
    X(SYNC_DRAWBUF)     /* ... the vertex draw buffer was full */        \
    X(SYNC_RETIRE)      /* ... retired texture memory needed freeing */  \
    X(SYNC_READBACK)    /* ... a framebuffer or depth readback */        \
    X(COMBINERS_MADE)   /* color combiners generated */                  \
    X(SHADERS_MADE)     /* shader programs generated */                  \
    X(DROPPED_FRAMES)   /* frames IsFrameReady refused */

enum Stage : uint8_t {
#define X(name) STAGE_##name,
    FAST3D_PROF_STAGES(X)
#undef X
        STAGE_COUNT
};

enum Counter : uint8_t {
#define X(name) COUNTER_##name,
    FAST3D_PROF_COUNTERS(X)
#undef X
        COUNTER_COUNT
};

// Experiments are switched on by name before the first frame (frigate: `--exp name=value`); all
// default to off. Each one removes or changes a piece of the frame so the effect on its time says
// what that piece costs. See profiler.cpp for what each does.
struct Experiments {
    int skip_tris = 0;      // GfxSpTri1 returns at once: no triangles reach the buffer
    int tri_limit = 0;      // emit at most this many triangles a frame (0 = no limit)
    int skip_draw = 0;      // Flush never calls the backend's DrawTriangles
    int point_filter = 0;   // force point sampling
    int no_blend = 0;       // never enable alpha blending
    int no_depth = 0;       // never enable depth test or mask
    int no_fog = 0;         // drop fog from every shader
    int measure_gpu = 0;    // wait for the GPU after the display list, so GPU_SYNC shows its time
    int swap_interval = 0;  // override the swap interval (1 = every vsync) on Wii U
    float resolution = 0.f; // internal resolution multiplier (0 = leave the setting)
};

#ifdef FAST3D_PROFILE

extern Experiments gExp;

// True if `name` is an experiment; sets it.
bool SetExperiment(const char* name, double value);
// "name=value" convenience for argument parsing; false for an unknown name or a malformed pair.
bool SetExperimentFromArg(const char* nameEqualsValue);

void Enter(Stage stage);
void Exit();
// Replaces the innermost stage in place (for a function that moves through its phases).
void Switch(Stage stage);
void Count(Counter counter, uint64_t n = 1);
void SetUserStageName(Stage userStage, const char* name);
// Marks the start of a frame's rendering work; logs the totals once a second.
void FrameBegin();
void FrameEnd();

struct Scope {
    explicit Scope(Stage stage) {
        Enter(stage);
    }
    ~Scope() {
        Exit();
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

#define FAST3D_PROF_CAT2(a, b) a##b
#define FAST3D_PROF_CAT(a, b) FAST3D_PROF_CAT2(a, b)
#define PROF_SCOPE(stage) ::Fast::Prof::Scope FAST3D_PROF_CAT(profScope, __LINE__)(::Fast::Prof::STAGE_##stage)
#define PROF_SWITCH(stage) ::Fast::Prof::Switch(::Fast::Prof::STAGE_##stage)
#define PROF_COUNT(counter) ::Fast::Prof::Count(::Fast::Prof::COUNTER_##counter)
#define PROF_COUNT_N(counter, n) ::Fast::Prof::Count(::Fast::Prof::COUNTER_##counter, (n))
#define PROF_EXP(field) (::Fast::Prof::gExp.field)

#else

#define PROF_SCOPE(stage) ((void)0)
#define PROF_SWITCH(stage) ((void)0)
#define PROF_COUNT(counter) ((void)0)
#define PROF_COUNT_N(counter, n) ((void)0)

#endif

} // namespace Fast::Prof
