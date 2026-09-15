/*  gfx_gx2.h - Fast3D GX2 backend for libultraship

    Created in 2022 by GaryOderNichts
*/
#ifdef __WIIU__
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <utility>

#include "gfx_rendering_api.h"

#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/surface.h>
#include <gx2/enum.h>

#include "fast/backends/gx2_shader_gen.h"
#include "ship/port/wiiu/ImGui/imgui_impl_gx2.h"

namespace Fast {

/**
 * @brief GX2 shader program metadata cached by the Fast3D renderer.
 */
struct ShaderProgram {
    struct ShaderGroup group;
    uint8_t num_inputs;
    bool used_textures[2];
    bool used_noise;
    uint32_t window_params_offset;
    int32_t samplers_location[SHADER_MAX_TEXTURES];
};

/**
 * @brief Nintendo Wii U (GX2) implementation of the Fast3D rendering API.
 */
class GfxRenderingAPIGX2 final : public GfxRenderingAPI {
  public:
    GfxRenderingAPIGX2() = default;
    ~GfxRenderingAPIGX2() override;

    /** @name GfxRenderingAPI implementation */
    /** @{ */
    const char* GetName() override;
    int GetMaxTextureSize() override;
    GfxClipParameters GetClipParameters() override;
    void UnloadShader(ShaderProgram* oldPrg) override;
    void LoadShader(ShaderProgram* newPrg) override;
    void ClearShaderCache() override;
    ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override;
    ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override;
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;
    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) override;
    void SetDepthTestAndMask(bool depth_test, bool z_upd) override;
    void SetZmodeDecal(bool decal) override;
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    void SetUseAlpha(bool useAlpha) override;
    void DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) override;
    void Init() override;
    void OnResize() override;
    void StartFrame() override;
    void EndFrame() override;
    void FinishRender() override;
    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                     bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                     bool can_extract_depth) override;
    void StartDrawToFramebuffer(int fbId, float noiseScale) override;
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0,
                         int dstX1, int dstY1) override;
    void ClearFramebuffer(bool color, bool depth) override;
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override;
    void ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) override;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) override;
    void* GetFramebufferTextureId(int fbId) override;
    void SelectTextureFb(int fbId) override;
    void DeleteTexture(uint32_t texId) override;
    void SetTextureFilter(FilteringMode mode) override;
    FilteringMode GetTextureFilter() override;
    void SetSrgbMode() override;
    ImTextureID GetTextureById(int id) override;
    void SetCurrentPrimDepth(float depth) override;
    /** @} */

  private:
    struct Texture {
        GX2Texture texture;
        bool texture_uploaded;

        GX2Sampler sampler;
        bool sampler_set;

        // For ImGui rendering
        ImGui_ImplGX2_Texture imtex;
    };

    struct Framebuffer {
        GX2ColorBuffer color_buffer;
        bool colorBufferMem1;
        GX2DepthBuffer depth_buffer;
        bool depthBufferMem1;

        GX2Texture texture;
        GX2Sampler sampler;

        // For ImGui rendering
        ImGui_ImplGX2_Texture imtex;
    };

    void InitFramebuffer(Framebuffer* buffer, uint32_t width, uint32_t height);
    void SetUniforms(ShaderProgram* prg);

    std::array<Framebuffer, 100> mFramebuffers{};
    std::size_t mUsedFramebuffers = 0;
    std::size_t mCurrentFramebuffer = 0;
    GX2DepthBuffer mDepthReadBuffer{};

    std::map<std::pair<uint64_t, uint64_t>, ShaderProgram> mShaderProgramPool;
    ShaderProgram* mCurrentShaderProgram = nullptr;

    Texture* mCurrentTexture = nullptr;
    int mCurrentTile = 0;

    uint8_t* mDrawBuffer = nullptr;
    uint8_t* mDrawPtr = nullptr;

    uint32_t mFrameCount = 0;
    float mNoiseScale = 0.0f;
    FilteringMode mFilterMode = FILTER_LINEAR;

    BOOL mDepthTest = TRUE;
    BOOL mDepthWrite = TRUE;
    GX2CompareFunction mDepthCompareFunction = GX2_COMPARE_FUNC_LESS;

    float mViewportX = 0.0f;
    float mViewportY = 0.0f;
    float mViewportWidth = 0.0f;
    float mViewportHeight = 0.0f;

    uint32_t mScissorX = 0;
    uint32_t mScissorY = 0;
    uint32_t mScissorWidth = 0;
    uint32_t mScissorHeight = 0;

    bool mZmodeDecal = false;
    float mSSDB = -2.0f;
    bool mUseAlpha = false;
};

} // namespace Fast
#endif
