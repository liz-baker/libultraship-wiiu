/*  gfx_gx2.cpp - Fast3D GX2 backend for libultraship

    Created in 2022 by GaryOderNichts
*/
#ifdef __WIIU__

#include "fast/backends/gfx_gx2.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <cassert>
#include <algorithm>
#include <bit>
#include <iterator>

#include "ship/window/Window.h"

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include "libultraship/libultra/gbi.h"
#include <public/bridge/consolevariablebridge.h>

#include "fast/interpreter.h"
#include "fast/backends/gfx_wiiu.h"
#include "gx2_util.h"

#include <gx2/texture.h>
#include <gx2/draw.h>
#include <gx2/clear.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <gx2/event.h>
#include <gx2/utils.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/display.h>

#include <proc_ui/procui.h>
#include <coreinit/memory.h>

#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

// 96 Mb (should be more than enough to draw everything without waiting for the GPU)
#define DRAW_BUFFER_SIZE 0x6000000

namespace Fast {

static inline GX2SamplerVar* GX2GetPixelSamplerVar(const GX2PixelShader* shader, const char* name) {
    for (uint32_t i = 0; i < shader->samplerVarCount; ++i) {
        if (strcmp(name, shader->samplerVars[i].name) == 0) {
            return &shader->samplerVars[i];
        }
    }

    return nullptr;
}

static inline int32_t GX2GetPixelSamplerVarLocation(const GX2PixelShader* shader, const char* name) {
    GX2SamplerVar* sampler = GX2GetPixelSamplerVar(shader, name);
    return sampler ? sampler->location : -1;
}

static inline int32_t GX2GetPixelUniformVarOffset(const GX2PixelShader* shader, const char* name) {
    GX2UniformVar* uniform = GX2GetPixelUniformVar(shader, name);
    return uniform ? uniform->offset : -1;
}

const char* GfxRenderingAPIGX2::GetName() {
    return "GX2";
}

int GfxRenderingAPIGX2::GetMaxTextureSize() {
    // TODO: This should be a define from the Wii U toolchain, but there isn't one yet
    return 8192;
}

void GfxRenderingAPIGX2::InitFramebuffer(Framebuffer* buffer, uint32_t width, uint32_t height) {
    memset(&buffer->color_buffer, 0, sizeof(GX2ColorBuffer));
    buffer->color_buffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    buffer->color_buffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    buffer->color_buffer.surface.width = width;
    buffer->color_buffer.surface.height = height;
    buffer->color_buffer.surface.depth = 1;
    buffer->color_buffer.surface.mipLevels = 1;
    buffer->color_buffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    buffer->color_buffer.surface.aa = GX2_AA_MODE1X;
    buffer->color_buffer.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    buffer->color_buffer.viewNumSlices = 1;

    memset(&buffer->depth_buffer, 0, sizeof(GX2DepthBuffer));
    buffer->depth_buffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER | GX2_SURFACE_USE_TEXTURE;
    buffer->depth_buffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    buffer->depth_buffer.surface.width = width;
    buffer->depth_buffer.surface.height = height;
    buffer->depth_buffer.surface.depth = 1;
    buffer->depth_buffer.surface.mipLevels = 1;
    buffer->depth_buffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_R32;
    buffer->depth_buffer.surface.aa = GX2_AA_MODE1X;
    buffer->depth_buffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    buffer->depth_buffer.viewNumSlices = 1;
    buffer->depth_buffer.depthClear = 1.0f;
}

GfxClipParameters GfxRenderingAPIGX2::GetClipParameters() {
    return { false, false };
}

void GfxRenderingAPIGX2::SetUniforms(ShaderProgram* prg) {
    float window_params_array[4] = { mNoiseScale, (float)mFrameCount, 0.0f, 0.0f };

    GX2SetPixelUniformReg(prg->window_params_offset, 4, window_params_array);
}

void GfxRenderingAPIGX2::UnloadShader(ShaderProgram* old_prg) {
    mCurrentShaderProgram = nullptr;
}

void GfxRenderingAPIGX2::LoadShader(ShaderProgram* new_prg) {
    mCurrentShaderProgram = new_prg;

    GX2SetFetchShader(&new_prg->group.fetchShader);
    GX2SetVertexShader(&new_prg->group.vertexShader);
    GX2SetPixelShader(&new_prg->group.pixelShader);

    SetUniforms(new_prg);
}

void GfxRenderingAPIGX2::ClearShaderCache() {
    mShaderProgramPool.clear();
    mCurrentShaderProgram = nullptr;
}

ShaderProgram* GfxRenderingAPIGX2::CreateAndLoadNewShader(uint64_t shader_id0, uint64_t shader_id1) {
    CCFeatures cc_features;
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);

    ShaderProgram* prg = &mShaderProgramPool[std::make_pair(shader_id0, shader_id1)];

    printf("Generating shader: %016llx-%016llx\n", (unsigned long long)shader_id0, (unsigned long long)shader_id1);
    if (gx2GenerateShaderGroup(&prg->group, &cc_features) != 0) {
        printf("Failed to generate shader\n");
        mCurrentShaderProgram = nullptr;
        return nullptr;
    }

    prg->num_inputs = cc_features.numInputs;
    prg->used_textures[0] = cc_features.usedTextures[0];
    prg->used_textures[1] = cc_features.usedTextures[1];

    LoadShader(prg);

    prg->window_params_offset = GX2GetPixelUniformVarOffset(&prg->group.pixelShader, "window_params");
    prg->samplers_location[0] = GX2GetPixelSamplerVarLocation(&prg->group.pixelShader, "uTex0");
    prg->samplers_location[1] = GX2GetPixelSamplerVarLocation(&prg->group.pixelShader, "uTex1");
    prg->samplers_location[2] = GX2GetPixelSamplerVarLocation(&prg->group.pixelShader, "uTexMask0");
    prg->samplers_location[3] = GX2GetPixelSamplerVarLocation(&prg->group.pixelShader, "uTexMask1");
    prg->samplers_location[4] = GX2GetPixelSamplerVarLocation(&prg->group.pixelShader, "uTexBlend0");
    prg->samplers_location[5] = GX2GetPixelSamplerVarLocation(&prg->group.pixelShader, "uTexBlend1");

    prg->used_noise = cc_features.opt_alpha && cc_features.opt_noise;

    printf("Generated and loaded shader\n");

    return prg;
}

ShaderProgram* GfxRenderingAPIGX2::LookupShader(uint64_t shader_id0, uint64_t shader_id1) {
    auto it = mShaderProgramPool.find(std::make_pair(shader_id0, shader_id1));
    return it == mShaderProgramPool.end() ? nullptr : &it->second;
}

void GfxRenderingAPIGX2::ShaderGetInfo(ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

uint32_t GfxRenderingAPIGX2::NewTexture() {
    Texture* tex = (Texture*)calloc(1, sizeof(Texture));

    tex->imtex.Texture = &tex->texture;
    tex->imtex.Sampler = &tex->sampler;

    // some 32-bit trickery :P
    return (uint32_t)tex;
}

void GfxRenderingAPIGX2::DeleteTexture(uint32_t texture_id) {
    Texture* tex = (Texture*)texture_id;

    if (tex->texture.surface.image) {
        free(tex->texture.surface.image);
    }

    free((void*)tex);
}

void GfxRenderingAPIGX2::SelectTexture(int tile, uint32_t texture_id) {
    Texture* tex = (Texture*)texture_id;
    mCurrentTexture = tex;
    mCurrentTile = tile;

    if (mCurrentShaderProgram) {
        int32_t sampler_location = mCurrentShaderProgram->samplers_location[tile];
        if (sampler_location != -1) {
            if (tex->texture_uploaded) {
                GX2SetPixelTexture(&tex->texture, sampler_location);
            }

            if (tex->sampler_set) {
                GX2SetPixelSampler(&tex->sampler, sampler_location);
            }
        }
    }
}

void GfxRenderingAPIGX2::UploadTexture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height) {
    Texture* tex = mCurrentTexture;
    assert(tex);

    if ((tex->texture.surface.width != width) || (tex->texture.surface.height != height) ||
        !tex->texture.surface.image) {

        if (tex->texture.surface.image) {
            free(tex->texture.surface.image);
            tex->texture.surface.image = nullptr;
        }

        memset(&tex->texture, 0, sizeof(GX2Texture));
        tex->texture.surface.use = GX2_SURFACE_USE_TEXTURE;
        tex->texture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        tex->texture.surface.width = width;
        tex->texture.surface.height = height;
        tex->texture.surface.depth = 1;
        tex->texture.surface.mipLevels = 1;
        tex->texture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        tex->texture.surface.aa = GX2_AA_MODE1X;
        tex->texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        tex->texture.viewFirstMip = 0;
        tex->texture.viewNumMips = 1;
        tex->texture.viewFirstSlice = 0;
        tex->texture.viewNumSlices = 1;
        tex->texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);

        GX2CalcSurfaceSizeAndAlignment(&tex->texture.surface);
        GX2InitTextureRegs(&tex->texture);

        tex->texture.surface.image = memalign(tex->texture.surface.alignment, tex->texture.surface.imageSize);
    }

    uint8_t* buf = (uint8_t*)tex->texture.surface.image;
    assert(buf);

    for (uint32_t y = 0; y < height; ++y) {
        memcpy(buf + (y * tex->texture.surface.pitch * 4), rgba32_buf + (y * width * 4), width * 4);
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->texture.surface.image, tex->texture.surface.imageSize);

    if (mCurrentShaderProgram && mCurrentShaderProgram->samplers_location[mCurrentTile] != -1) {
        GX2SetPixelTexture(&tex->texture, mCurrentShaderProgram->samplers_location[mCurrentTile]);
    }

    tex->texture_uploaded = true;
}

static GX2TexClampMode gfx_cm_to_gx2(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GX2_TEX_CLAMP_MODE_CLAMP;
        case G_TX_MIRROR | G_TX_WRAP:
            return GX2_TEX_CLAMP_MODE_MIRROR;
        case G_TX_MIRROR | G_TX_CLAMP:
            return GX2_TEX_CLAMP_MODE_MIRROR_ONCE;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GX2_TEX_CLAMP_MODE_WRAP;
    }

    return GX2_TEX_CLAMP_MODE_WRAP;
}

void GfxRenderingAPIGX2::SetSamplerParameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    Texture* tex = mCurrentTexture;
    assert(tex);

    mCurrentTile = tile;

    GX2InitSampler(&tex->sampler, GX2_TEX_CLAMP_MODE_CLAMP,
                   (linear_filter && mFilterMode == FILTER_LINEAR) ? GX2_TEX_XY_FILTER_MODE_LINEAR
                                                                   : GX2_TEX_XY_FILTER_MODE_POINT);

    GX2InitSamplerClamping(&tex->sampler, gfx_cm_to_gx2(cms), gfx_cm_to_gx2(cmt), GX2_TEX_CLAMP_MODE_WRAP);

    if (mCurrentShaderProgram && mCurrentShaderProgram->samplers_location[tile] != -1) {
        GX2SetPixelSampler(&tex->sampler, mCurrentShaderProgram->samplers_location[tile]);
    }

    tex->sampler_set = true;
}

void GfxRenderingAPIGX2::SetDepthTestAndMask(bool depth_test, bool z_upd) {
    mDepthTest = depth_test || z_upd;
    mDepthWrite = z_upd;
    mDepthCompareFunction = depth_test ? GX2_COMPARE_FUNC_LEQUAL : GX2_COMPARE_FUNC_ALWAYS;

    GX2SetDepthOnlyControl(mDepthTest, mDepthWrite, mDepthCompareFunction);
}

void GfxRenderingAPIGX2::SetZmodeDecal(bool zmode_decal) {
    mZmodeDecal = zmode_decal;
    if (zmode_decal) {
        // SSDB = SlopeScaledDepthBias 120 leads to -2 at 240p which is the same as N64 mode which has very little
        // fighting
        const int n64modeFactor = 120;
        const int noVanishFactor = 100;
        float SSDB = -2.0f;
        switch (CVarGetInteger("gZFightingMode", 0)) {
            // scaled z-fighting (N64 mode like)
            case 1:
                if (mCurrentFramebuffer < mUsedFramebuffers) {
                    SSDB =
                        -1.0f * (float)mFramebuffers[mCurrentFramebuffer].color_buffer.surface.height / n64modeFactor;
                }
                break;
            // no vanishing paths
            case 2:
                if (mCurrentFramebuffer < mUsedFramebuffers) {
                    SSDB =
                        -1.0f * (float)mFramebuffers[mCurrentFramebuffer].color_buffer.surface.height / noVanishFactor;
                }
                break;
            // disabled
            case 0:
            default:
                SSDB = -2.0f;
        }

        mSSDB = SSDB;
        GX2SetPolygonOffset(SSDB, SSDB, SSDB, SSDB, 0.0f);
        GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, TRUE, GX2_POLYGON_MODE_TRIANGLE,
                             GX2_POLYGON_MODE_TRIANGLE, TRUE, TRUE, FALSE);
    } else {
        GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE,
                             GX2_POLYGON_MODE_TRIANGLE, FALSE, FALSE, FALSE);
    }
}

void GfxRenderingAPIGX2::SetViewport(int x, int y, int width, int height) {
    Framebuffer& buffer = mFramebuffers[mCurrentFramebuffer];
    uint32_t buffer_height = buffer.color_buffer.surface.height;

    mViewportX = x;
    mViewportY = buffer_height - y - height;
    mViewportWidth = width;
    mViewportHeight = height;

    GX2SetViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight, 0.0f, 1.0f);
}

void GfxRenderingAPIGX2::SetScissor(int x, int y, int width, int height) {
    Framebuffer& buffer = mFramebuffers[mCurrentFramebuffer];
    uint32_t buffer_height = buffer.color_buffer.surface.height;
    uint32_t buffer_width = buffer.color_buffer.surface.width;

    mScissorX = std::min((uint32_t)width, (uint32_t)x);
    mScissorY = std::min((uint32_t)height, buffer_height - y - height);
    mScissorWidth = std::min((uint32_t)width, buffer_width);
    mScissorHeight = std::min((uint32_t)height, buffer_height);

    GX2SetScissor(mScissorX, mScissorY, mScissorWidth, mScissorHeight);
}

void GfxRenderingAPIGX2::SetUseAlpha(bool use_alpha) {
    mUseAlpha = use_alpha;
    GX2SetColorControl(GX2_LOGIC_OP_COPY, use_alpha ? 0xff : 0, FALSE, TRUE);
}

void GfxRenderingAPIGX2::DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (!mCurrentShaderProgram) {
        return;
    }

    size_t vbo_len = sizeof(float) * buf_vbo_len;

    if (mDrawPtr + vbo_len >= mDrawBuffer + DRAW_BUFFER_SIZE) {
        printf("Waiting on GPU!!!\n");
        GX2DrawDone();
        mDrawPtr = mDrawBuffer;
    }

    float* new_vbo = (float*)mDrawPtr;
    mDrawPtr += ALIGN(vbo_len, GX2_VERTEX_BUFFER_ALIGNMENT);

    OSBlockMove(new_vbo, buf_vbo, vbo_len, FALSE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, new_vbo, vbo_len);

    GX2SetAttribBuffer(0, vbo_len, mCurrentShaderProgram->group.stride, new_vbo);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3 * buf_vbo_num_tris, 0, 1);
}

void GfxRenderingAPIGX2::Init() {
    mViewportWidth = mScissorWidth = WIIU_DEFAULT_FB_WIDTH;
    mViewportHeight = mScissorHeight = WIIU_DEFAULT_FB_HEIGHT;

    // Init the default framebuffer
    mUsedFramebuffers = 1;
    Framebuffer& main_framebuffer = mFramebuffers[0];

    InitFramebuffer(&main_framebuffer, WIIU_DEFAULT_FB_WIDTH, WIIU_DEFAULT_FB_HEIGHT);

    GX2CalcSurfaceSizeAndAlignment(&main_framebuffer.color_buffer.surface);
    GX2InitColorBufferRegs(&main_framebuffer.color_buffer);

    main_framebuffer.color_buffer.surface.image = gfx_wiiu_alloc_mem1(main_framebuffer.color_buffer.surface.imageSize,
                                                                      main_framebuffer.color_buffer.surface.alignment);
    assert(main_framebuffer.color_buffer.surface.image);

    GX2CalcSurfaceSizeAndAlignment(&main_framebuffer.depth_buffer.surface);
    GX2InitDepthBufferRegs(&main_framebuffer.depth_buffer);

    main_framebuffer.depth_buffer.surface.image = gfx_wiiu_alloc_mem1(main_framebuffer.depth_buffer.surface.imageSize,
                                                                      main_framebuffer.depth_buffer.surface.alignment);
    assert(main_framebuffer.depth_buffer.surface.image);

    main_framebuffer.imtex.Texture = &main_framebuffer.texture;
    main_framebuffer.imtex.Sampler = &main_framebuffer.sampler;

    // create a linear aligned copy of the depth buffer to read pixels to
    memcpy(&mDepthReadBuffer, &main_framebuffer.depth_buffer, sizeof(GX2DepthBuffer));

    mDepthReadBuffer.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    mDepthReadBuffer.surface.width = 32;
    mDepthReadBuffer.surface.height = 1;

    GX2CalcSurfaceSizeAndAlignment(&mDepthReadBuffer.surface);

    mDepthReadBuffer.surface.image =
        gfx_wiiu_alloc_mem1(mDepthReadBuffer.surface.alignment, mDepthReadBuffer.surface.imageSize);
    assert(mDepthReadBuffer.surface.image);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_DEPTH_BUFFER, mDepthReadBuffer.surface.image,
                  mDepthReadBuffer.surface.imageSize);

    GX2SetColorBuffer(&main_framebuffer.color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&main_framebuffer.depth_buffer);

    mCurrentFramebuffer = 0;

    // allocate draw buffer
    mDrawBuffer = (uint8_t*)memalign(GX2_VERTEX_BUFFER_ALIGNMENT, DRAW_BUFFER_SIZE);
    assert(mDrawBuffer);
    mDrawPtr = mDrawBuffer;

    GX2SetRasterizerClipControl(TRUE, FALSE);

    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
                       GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                       GX2_BLEND_COMBINE_MODE_ADD);

    GX2Util::Init();
    gfx_wiiu_set_context_state();
}

GfxRenderingAPIGX2::~GfxRenderingAPIGX2() {
    if (has_foreground) {
        GX2DrawDone();

        if (mDepthReadBuffer.surface.image) {
            gfx_wiiu_free_mem1(mDepthReadBuffer.surface.image);
            mDepthReadBuffer.surface.image = nullptr;
        }

        for (auto& buffer : mFramebuffers) {
            if (buffer.texture.surface.image) {
                if (buffer.colorBufferMem1) {
                    gfx_wiiu_free_mem1(buffer.texture.surface.image);
                } else {
                    free(buffer.texture.surface.image);
                }
                buffer.texture.surface.image = nullptr;
            }

            if (buffer.depth_buffer.surface.image) {
                if (buffer.depthBufferMem1) {
                    gfx_wiiu_free_mem1(buffer.depth_buffer.surface.image);
                } else {
                    free(buffer.depth_buffer.surface.image);
                }
                buffer.depth_buffer.surface.image = nullptr;
            }
        }
    }

    if (mDrawBuffer) {
        free(mDrawBuffer);
        mDrawBuffer = nullptr;
        mDrawPtr = nullptr;
    }

    GX2Util::Shutdown();
}

void GfxRenderingAPIGX2::OnResize() {
}

void GfxRenderingAPIGX2::StartFrame() {
    // Restore state since ImGui modified it when rendering
    GX2SetViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight, 0.0f, 1.0f);
    GX2SetScissor(mScissorX, mScissorY, mScissorWidth, mScissorHeight);

    GX2SetColorControl(GX2_LOGIC_OP_COPY, mUseAlpha ? 0xff : 0, FALSE, TRUE);

    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
                       GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                       GX2_BLEND_COMBINE_MODE_ADD);

    GX2SetDepthOnlyControl(mDepthTest, mDepthWrite, mDepthCompareFunction);

    if (mZmodeDecal) {
        GX2SetPolygonOffset(mSSDB, mSSDB, mSSDB, mSSDB, 0.0f);
        GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, TRUE, GX2_POLYGON_MODE_TRIANGLE,
                             GX2_POLYGON_MODE_TRIANGLE, TRUE, TRUE, FALSE);
    } else {
        GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE,
                             GX2_POLYGON_MODE_TRIANGLE, FALSE, FALSE, FALSE);
    }

    mFrameCount++;
}

void GfxRenderingAPIGX2::EndFrame() {
    mDrawPtr = mDrawBuffer;

    Framebuffer& main_framebuffer = mFramebuffers[0];

    GX2CopyColorBufferToScanBuffer(&main_framebuffer.color_buffer, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(&main_framebuffer.color_buffer, GX2_SCAN_TARGET_DRC);
}

void GfxRenderingAPIGX2::FinishRender() {
}

int GfxRenderingAPIGX2::CreateFramebuffer() {
    assert(mUsedFramebuffers < mFramebuffers.size());

    std::size_t i = mUsedFramebuffers;
    mUsedFramebuffers++;

    Framebuffer& buffer = mFramebuffers[i];

    GX2InitSampler(&buffer.sampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);

    buffer.imtex.Texture = &buffer.texture;
    buffer.imtex.Sampler = &buffer.sampler;

    return i;
}

void GfxRenderingAPIGX2::UpdateFramebufferParameters(int fb, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {
    // we don't support updating the main buffer (fb 0)
    if (fb == 0) {
        return;
    }

    Framebuffer& buffer = mFramebuffers[fb];

    if (buffer.texture.surface.width == width && buffer.texture.surface.height == height) {
        return;
    }

    // make sure the GPU no longer writes to the buffer
    GX2DrawDone();

    if (buffer.texture.surface.image) {
        if (buffer.colorBufferMem1) {
            gfx_wiiu_free_mem1(buffer.texture.surface.image);
        } else {
            free(buffer.texture.surface.image);
        }
        buffer.texture.surface.image = nullptr;
    }

    if (buffer.depth_buffer.surface.image) {
        if (buffer.depthBufferMem1) {
            gfx_wiiu_free_mem1(buffer.depth_buffer.surface.image);
        } else {
            free(buffer.depth_buffer.surface.image);
        }
        buffer.depth_buffer.surface.image = nullptr;
    }

    InitFramebuffer(&buffer, width, height);

    GX2CalcSurfaceSizeAndAlignment(&buffer.depth_buffer.surface);
    GX2InitDepthBufferRegs(&buffer.depth_buffer);

    buffer.depth_buffer.surface.image =
        gfx_wiiu_alloc_mem1(buffer.depth_buffer.surface.imageSize, buffer.depth_buffer.surface.alignment);
    // fall back to mem2
    if (!buffer.depth_buffer.surface.image) {
        buffer.depth_buffer.surface.image =
            memalign(buffer.depth_buffer.surface.alignment, buffer.depth_buffer.surface.imageSize);
        buffer.depthBufferMem1 = false;
    } else {
        buffer.depthBufferMem1 = true;
    }
    assert(buffer.depth_buffer.surface.image);

    GX2CalcSurfaceSizeAndAlignment(&buffer.color_buffer.surface);
    GX2InitColorBufferRegs(&buffer.color_buffer);

    memset(&buffer.texture, 0, sizeof(GX2Texture));
    buffer.texture.surface.use = GX2_SURFACE_USE_TEXTURE;
    buffer.texture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    buffer.texture.surface.width = width;
    buffer.texture.surface.height = height;
    buffer.texture.surface.depth = 1;
    buffer.texture.surface.mipLevels = 1;
    buffer.texture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    buffer.texture.surface.aa = GX2_AA_MODE1X;
    buffer.texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    buffer.texture.viewFirstMip = 0;
    buffer.texture.viewNumMips = 1;
    buffer.texture.viewFirstSlice = 0;
    buffer.texture.viewNumSlices = 1;
    buffer.texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);

    GX2CalcSurfaceSizeAndAlignment(&buffer.texture.surface);
    GX2InitTextureRegs(&buffer.texture);

    // the texture and color buffer share a buffer
    assert(buffer.color_buffer.surface.imageSize == buffer.texture.surface.imageSize);

    buffer.texture.surface.image =
        gfx_wiiu_alloc_mem1(buffer.texture.surface.imageSize, buffer.texture.surface.alignment);
    // fall back to mem2
    if (!buffer.texture.surface.image) {
        buffer.texture.surface.image = memalign(buffer.texture.surface.alignment, buffer.texture.surface.imageSize);
        buffer.colorBufferMem1 = false;
    } else {
        buffer.colorBufferMem1 = true;
    }
    assert(buffer.texture.surface.image);

    buffer.color_buffer.surface.image = buffer.texture.surface.image;
}

void GfxRenderingAPIGX2::StartDrawToFramebuffer(int fb, float noise_scale) {
    Framebuffer& buffer = mFramebuffers[fb];

    if (noise_scale != 0.0f) {
        mNoiseScale = 1.0f / noise_scale;
    }

    GX2SetColorBuffer(&buffer.color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&buffer.depth_buffer);

    mCurrentFramebuffer = fb;
}

void GfxRenderingAPIGX2::ClearFramebuffer(bool color, bool depth) {
    Framebuffer& buffer = mFramebuffers[mCurrentFramebuffer];

    if (color) {
        GX2ClearColor(&buffer.color_buffer, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (depth) {
        GX2ClearDepthStencilEx(&buffer.depth_buffer, buffer.depth_buffer.depthClear, buffer.depth_buffer.stencilClear,
                               GX2_CLEAR_FLAGS_BOTH);
    }

    gfx_wiiu_set_context_state();
}

void GfxRenderingAPIGX2::ResolveMSAAColorBuffer(int fb_id_target, int fb_id_source) {
    Framebuffer& src_buffer = mFramebuffers[fb_id_source];
    Framebuffer& target_buffer = mFramebuffers[fb_id_target];

    if (src_buffer.color_buffer.surface.aa == GX2_AA_MODE1X) {
        GX2CopySurface(&src_buffer.color_buffer.surface, src_buffer.color_buffer.viewMip,
                       src_buffer.color_buffer.viewFirstSlice, &target_buffer.color_buffer.surface,
                       target_buffer.color_buffer.viewMip, target_buffer.color_buffer.viewFirstSlice);
    } else {
        GX2ResolveAAColorBuffer(&src_buffer.color_buffer, &target_buffer.color_buffer.surface,
                                target_buffer.color_buffer.viewMip, target_buffer.color_buffer.viewFirstSlice);
    }
}

void* GfxRenderingAPIGX2::GetFramebufferTextureId(int fb_id) {
    Framebuffer& buffer = mFramebuffers[fb_id];

    return &buffer.imtex;
}

void GfxRenderingAPIGX2::SelectTextureFb(int fb) {
    Framebuffer& buffer = mFramebuffers[fb];

    assert(mCurrentShaderProgram);
    uint32_t location = mCurrentShaderProgram->samplers_location[0];
    GX2SetPixelTexture(&buffer.texture, location);
    GX2SetPixelSampler(&buffer.sampler, location);
}

void GfxRenderingAPIGX2::CopyFramebuffer(int fb_dst_id, int fb_src_id, int srcX0, int srcY0, int srcX1, int srcY1,
                                         int dstX0, int dstY0, int dstX1, int dstY1) {
    if ((std::size_t)fb_dst_id >= mUsedFramebuffers || (std::size_t)fb_src_id >= mUsedFramebuffers) {
        return;
    }

    Framebuffer& dst_buffer = mFramebuffers[fb_dst_id];
    Framebuffer& src_buffer = mFramebuffers[fb_src_id];

    int32_t fb_width = src_buffer.color_buffer.surface.width;
    int32_t fb_height = src_buffer.color_buffer.surface.height;
    srcX0 = std::clamp(srcX0, 0, fb_width);
    srcX1 = std::clamp(srcX1, 0, fb_width);
    srcY0 = std::clamp(srcY0, 0, fb_height);
    srcY1 = std::clamp(srcY1, 0, fb_height);

    GX2Rect src = { srcX0, srcY0, srcX1, srcY1 };
    GX2Point dst = { dstX0, dstY0 };
    GX2CopySurfaceEx(&src_buffer.color_buffer.surface, 0, 0, &dst_buffer.color_buffer.surface, 0, 0, 1, &src, &dst);

    gfx_wiiu_set_context_state();
}

void GfxRenderingAPIGX2::ReadFramebufferToCPU(int fb_id, uint32_t width, uint32_t height, uint16_t* rgba16_buf) {
    if ((std::size_t)fb_id >= mUsedFramebuffers) {
        return;
    }

    Framebuffer& buffer = mFramebuffers[fb_id];

    // Create a temporary linear surface in the correct format
    GX2Surface surface;
    memset(&surface, 0, sizeof(GX2Surface));
    surface.use = GX2_SURFACE_USE_TEXTURE;
    surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    surface.width = width;
    surface.height = height;
    surface.depth = 1;
    surface.mipLevels = 1;
    surface.format = GX2_SURFACE_FORMAT_UNORM_A1_B5_G5_R5; // GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
    surface.aa = GX2_AA_MODE1X;
    surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&surface);

    surface.image = memalign(surface.alignment, surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface.image, surface.imageSize);

    GX2Util::ConvertSurface(&buffer.color_buffer.surface, &surface);
    GX2DrawDone();

    gfx_wiiu_set_context_state();

    for (uint32_t y = 0; y < height; y++) {
        memcpy(rgba16_buf + y * width, ((uint16_t*)surface.image) + y * surface.pitch, width * 2);
    }

    free(surface.image);
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPIGX2::GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) {
    Framebuffer& buffer = mFramebuffers[fb_id];

    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> res;
    GX2Rect srcRects[25];
    GX2Point dstPoints[25];
    size_t num_coordinates = coordinates.size();
    while (num_coordinates > 0) {
        size_t numRects = 25;
        if (num_coordinates < numRects) {
            numRects = num_coordinates;
        }
        num_coordinates -= numRects;

        // initialize rects and points
        for (size_t i = 0; i < numRects; ++i) {
            const auto& c = *std::next(coordinates.begin(), num_coordinates + i);
            const int32_t x = (int32_t)std::clamp(c.first, 0.0f, (float)(buffer.depth_buffer.surface.width - 1));
            const int32_t y = (int32_t)std::clamp(c.second, 0.0f, (float)(buffer.depth_buffer.surface.height - 1));

            srcRects[i] = GX2Rect{ x, (int32_t)buffer.depth_buffer.surface.height - y, x + 1,
                                   (int32_t)(buffer.depth_buffer.surface.height - y) + 1 };

            // dst points will be spread over the x-axis of the buffer
            dstPoints[i] = GX2Point{ (int32_t)i, 0 };
        }

        // Invalidate the buffer first
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_DEPTH_BUFFER, mDepthReadBuffer.surface.image,
                      mDepthReadBuffer.surface.imageSize);

        // Perform the copy
        GX2CopySurfaceEx(&buffer.depth_buffer.surface, 0, 0, &mDepthReadBuffer.surface, 0, 0, numRects, srcRects,
                         dstPoints);

        // Wait for draws to be done and restore context, in case GPU was used
        GX2DrawDone();
        gfx_wiiu_set_context_state();

        // read the pixels from the depthReadBuffer
        for (size_t i = 0; i < numRects; ++i) {
            uint32_t tmp = __builtin_bswap32(*((uint32_t*)mDepthReadBuffer.surface.image + i));
            float val = std::bit_cast<float>(tmp);

            const auto& c = *std::next(coordinates.begin(), num_coordinates + i);
            res.emplace(c, val * 65532.0f);
        }
    }

    return res;
}

void GfxRenderingAPIGX2::SetTextureFilter(FilteringMode mode) {
    // three-point is not implemented in the shaders yet
    if (mode == FILTER_THREE_POINT) {
        mode = FILTER_LINEAR;
    }

    mFilterMode = mode;
    gfx_texture_cache_clear();
}

FilteringMode GfxRenderingAPIGX2::GetTextureFilter() {
    return mFilterMode;
}

ImTextureID GfxRenderingAPIGX2::GetTextureById(int id) {
    Texture* tex = (Texture*)id;
    return reinterpret_cast<ImTextureID>(&tex->imtex);
}

void GfxRenderingAPIGX2::SetSrgbMode() {
}

void GfxRenderingAPIGX2::SetCurrentPrimDepth(float depth) {
    // GX2 has no prim-depth uniform; store for interface completeness.
    mCurrentPrimDepth = depth;
}

} // namespace Fast

#endif
