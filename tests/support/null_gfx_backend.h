#pragma once

// Test-only GfxRenderingAPI / GfxWindowBackend that do no GPU or window work. The rendering API
// records the calls the interpreter makes on it, so tests can assert on what reached the backend
// (texture uploads, sampler state, shaders, draws) without a GPU. See issue #31.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fast/interpreter.h"
#include "fast/backends/gfx_rendering_api.h"
#include "fast/backends/gfx_window_manager_api.h"
#include "fast/debug/GfxDebugger.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"

namespace Fast {

// Defined in interpreter.cpp; the free-function opcode handlers reach the interpreter through it.
void GfxSetInstance(std::shared_ptr<Interpreter> gfx);

class NullGfxRenderingAPI final : public GfxRenderingAPI {
  public:
    struct Upload {
        int tile;
        uint32_t textureId;
        uint32_t width, height;
        std::vector<uint8_t> rgba32;
        // Set when this upload came through UploadTextureMipChain(): the chain's levels after the
        // first, which width/height/rgba32 describe.
        std::vector<Upload> lowerLevels;
    };
    struct SamplerCall {
        int sampler;
        uint32_t textureId;
        bool linearFilter;
        uint32_t cms, cmt;
    };
    struct Draw {
        uint64_t shaderId0, shaderId1;
        size_t numTris;
        std::vector<float> vbo;
    };

    std::vector<Upload> uploads;
    std::vector<SamplerCall> samplerCalls;
    std::vector<std::pair<uint64_t, uint64_t>> shadersCreated;
    std::vector<Draw> draws;
    std::vector<uint32_t> deletedTextures;
    std::vector<float> lodBiasCalls;

    void ClearRecording() {
        uploads.clear();
        samplerCalls.clear();
        shadersCreated.clear();
        draws.clear();
        deletedTextures.clear();
        lodBiasCalls.clear();
    }

    const char* GetName() override {
        return "Null";
    }
    int GetMaxTextureSize() override {
        // Small enough that the interpreter's upload staging buffer stays cheap in tests.
        return 1024;
    }
    GfxClipParameters GetClipParameters() override {
        return { false, false };
    }

    void UnloadShader(ShaderProgram* oldPrg) override {
    }
    void LoadShader(ShaderProgram* newPrg) override {
        mCurrentShader = AsNull(newPrg);
    }
    void ClearShaderCache() override {
        mShaders.clear();
        mCurrentShader = nullptr;
    }
    ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) override {
        auto prg = std::make_unique<NullShaderProgram>();
        prg->shaderId0 = shaderId0;
        prg->shaderId1 = shaderId1;
        // Same feature decode every real backend runs, so ShaderGetInfo() answers the way theirs would.
        CCFeatures features{};
        gfx_cc_get_features(shaderId0, shaderId1, &features);
        prg->numInputs = (uint8_t)features.numInputs;
        prg->usedTextures[0] = features.usedTextures[0];
        prg->usedTextures[1] = features.usedTextures[1];
        mCurrentShader = prg.get();
        mShaders.push_back(std::move(prg));
        shadersCreated.emplace_back(shaderId0, shaderId1);
        return AsShader(mCurrentShader);
    }
    ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) override {
        for (auto& prg : mShaders) {
            if (prg->shaderId0 == shaderId0 && prg->shaderId1 == shaderId1) {
                return AsShader(prg.get());
            }
        }
        return nullptr;
    }
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override {
        NullShaderProgram* p = AsNull(prg);
        *numInputs = p->numInputs;
        usedTextures[0] = p->usedTextures[0];
        usedTextures[1] = p->usedTextures[1];
    }

    uint32_t NewTexture() override {
        return mNextTextureId++;
    }
    void SelectTexture(int tile, uint32_t textureId) override {
        mCurrentTile = tile;
        if (tile >= 0 && tile < SHADER_MAX_TEXTURES) {
            mSelected[tile] = textureId;
        }
    }
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override {
        Upload up{ mCurrentTile, SelectedTexture(), width, height, {}, {} };
        up.rgba32.assign(rgba32Buf, rgba32Buf + (size_t)width * height * 4);
        uploads.push_back(std::move(up));
    }
    void UploadTextureMipChain(const TextureMipLevel* levels, uint32_t numLevels) override {
        auto record = [&](const TextureMipLevel& level) {
            Upload up{ mCurrentTile, SelectedTexture(), level.width, level.height, {}, {} };
            up.rgba32.assign(level.rgba32, level.rgba32 + (size_t)level.width * level.height * 4);
            return up;
        };
        Upload base = record(levels[0]);
        for (uint32_t n = 1; n < numLevels; n++) {
            base.lowerLevels.push_back(record(levels[n]));
        }
        uploads.push_back(std::move(base));
    }
    void SetTextureLodBias(float bias) override {
        lodBiasCalls.push_back(bias);
    }
    void SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) override {
        uint32_t textureId = (sampler >= 0 && sampler < SHADER_MAX_TEXTURES) ? mSelected[sampler] : 0;
        samplerCalls.push_back({ sampler, textureId, linear_filter, cms, cmt });
    }

    void SetDepthTestAndMask(bool depth_test, bool z_upd) override {
    }
    void SetZmodeDecal(bool decal) override {
    }
    void SetViewport(int x, int y, int width, int height) override {
    }
    void SetScissor(int x, int y, int width, int height) override {
    }
    void SetUseAlpha(bool useAlpha) override {
    }
    void DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) override {
        Draw d{ 0, 0, buf_vbo_num_tris, std::vector<float>(buf_vbo, buf_vbo + buf_vbo_len) };
        if (mCurrentShader != nullptr) {
            d.shaderId0 = mCurrentShader->shaderId0;
            d.shaderId1 = mCurrentShader->shaderId1;
        }
        draws.push_back(std::move(d));
    }

    void Init() override {
    }
    void OnResize() override {
    }
    void StartFrame() override {
    }
    void EndFrame() override {
    }
    void FinishRender() override {
    }
    int CreateFramebuffer() override {
        return mNextFramebufferId++;
    }
    void UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                     bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                     bool can_extract_depth) override {
    }
    void StartDrawToFramebuffer(int fbId, float noiseScale) override {
    }
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0,
                         int dstX1, int dstY1) override {
    }
    void ClearFramebuffer(bool color, bool depth) override {
    }
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override {
        std::memset(rgba16Buf, 0, (size_t)width * height * sizeof(uint16_t));
    }
    void ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) override {
    }
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) override {
        std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> res;
        for (const auto& c : coordinates) {
            res.emplace(c, 0);
        }
        return res;
    }
    void* GetFramebufferTextureId(int fbId) override {
        return nullptr;
    }
    void SelectTextureFb(int fbId) override {
    }
    void DeleteTexture(uint32_t texId) override {
        deletedTextures.push_back(texId);
    }
    void SetTextureFilter(FilteringMode mode) override {
        mFilterMode = mode;
    }
    FilteringMode GetTextureFilter() override {
        return mFilterMode;
    }
    void SetSrgbMode() override {
    }
    ImTextureID GetTextureById(int id) override {
        return (ImTextureID)0;
    }
    void SetCurrentPrimDepth(float depth) override {
    }

  private:
    // Kept separate from the real backends' ShaderProgram definitions (one per backend .cpp), which
    // this test binary also links; the interpreter only ever treats it as an opaque pointer.
    struct NullShaderProgram {
        uint64_t shaderId0 = 0, shaderId1 = 0;
        uint8_t numInputs = 0;
        bool usedTextures[2] = { false, false };
    };
    static ShaderProgram* AsShader(NullShaderProgram* p) {
        return reinterpret_cast<ShaderProgram*>(p);
    }
    static NullShaderProgram* AsNull(ShaderProgram* p) {
        return reinterpret_cast<NullShaderProgram*>(p);
    }
    uint32_t SelectedTexture() const {
        return (mCurrentTile >= 0 && mCurrentTile < SHADER_MAX_TEXTURES) ? mSelected[mCurrentTile] : 0;
    }

    std::vector<std::unique_ptr<NullShaderProgram>> mShaders;
    NullShaderProgram* mCurrentShader = nullptr;
    uint32_t mNextTextureId = 1;
    int mNextFramebufferId = 0;
    int mCurrentTile = 0;
    uint32_t mSelected[SHADER_MAX_TEXTURES] = {};
    FilteringMode mFilterMode = FILTER_LINEAR;
};

class NullGfxWindowBackend final : public GfxWindowBackend {
  public:
    void Init(const char* gameName, const char* apiName, bool startFullScreen, uint32_t width, uint32_t height,
              int32_t posX, int32_t posY) override {
        mWidth = width;
        mHeight = height;
        mFullScreen = startFullScreen;
    }
    void Close() override {
        mIsRunning = false;
    }
    void SetKeyboardCallbacks(bool (*onKeyDown)(int scancode), bool (*onKeyUp)(int scancode),
                              void (*onAllKeysUp)()) override {
    }
    void SetMouseCallbacks(bool (*onMouseButtonDown)(int btn), bool (*onMouseButtonUp)(int btn)) override {
    }
    void SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool is_now_fullscreen)) override {
    }
    void SetFullscreen(bool fullscreen) override {
        mFullScreen = fullscreen;
    }
    void GetActiveWindowRefreshRate(uint32_t* refreshRate) override {
        *refreshRate = 60;
    }
    void SetCursorVisibility(bool visability) override {
    }
    void SetMousePos(int32_t posX, int32_t posY) override {
    }
    void GetMousePos(int32_t* x, int32_t* y) override {
        *x = *y = 0;
    }
    void GetMouseDelta(int32_t* x, int32_t* y) override {
        *x = *y = 0;
    }
    void GetMouseWheel(float* x, float* y) override {
        *x = *y = 0.0f;
    }
    bool GetMouseState(uint32_t btn) override {
        return false;
    }
    void SetMouseCapture(bool capture) override {
    }
    bool IsMouseCaptured() override {
        return false;
    }
    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) override {
        *width = mWidth;
        *height = mHeight;
        *posX = 0;
        *posY = 0;
    }
    void SetDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) override {
        mWidth = width;
        mHeight = height;
    }
    Ship::WindowRect GetPrimaryMonitorRect() override {
        return {};
    }
    void HandleEvents() override {
    }
    bool IsFrameReady() override {
        return true;
    }
    void SwapBuffersBegin() override {
    }
    void SwapBuffersEnd() override {
    }
    double GetTime() override {
        return 0.0;
    }
    int GetTargetFps() override {
        return mTargetFps;
    }
    void SetTargetFps(int fps) override {
        mTargetFps = fps;
    }
    void SetMaxFrameLatency(int latency) override {
    }
    const char* GetKeyName(int scancode) override {
        return "";
    }
    bool CanDisableVsync() override {
        return false;
    }
    bool IsRunning() override {
        return mIsRunning;
    }
    void Destroy() override {
    }
    bool IsFullscreen() override {
        return mFullScreen;
    }

  private:
    uint32_t mWidth = 0, mHeight = 0;
};

// A real Interpreter, Init()ed against the null backends, for feeding display lists through Run().
// No Context, window, GPU or resource manager: display lists must use raw addresses, not OTR paths.
class NullBackendInterpreter {
  public:
    static constexpr uint32_t kWidth = 320;
    static constexpr uint32_t kHeight = 240;

    NullBackendInterpreter() {
        // ConsoleVariable loads from a Config, which creates its JSON file if it doesn't exist.
        mConfigPath = (std::filesystem::temp_directory_path() /
                       ("lus_null_backend_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".json"))
                          .string();
        mConfig = std::make_shared<Ship::Config>(mConfigPath);
        cvars = std::make_shared<Ship::ConsoleVariable>(mConfig);

        interpreter = std::make_shared<Interpreter>();
        GfxSetInstance(interpreter);
        interpreter->SetGfxDebugger(std::make_shared<GfxDebugger>());
        interpreter->Init(&wapi, &rapi, "lus-tests", false, kWidth, kHeight, 0, 0, cvars, nullptr);
        // Run() sizes the frame from the current window dimensions, which StartFrame() normally fills
        // in from the window backend.
        interpreter->mGfxCurrentWindowDimensions = { 1.0f, kWidth, kHeight, (float)kWidth / kHeight };
        interpreter->mCurDimensions = interpreter->mGfxCurrentWindowDimensions;
        gfx_set_target_ucode(ucode_f3dex2);
    }

    ~NullBackendInterpreter() {
        interpreter->Destroy();
        GfxSetInstance(nullptr);
        interpreter.reset();
        cvars.reset();
        mConfig.reset();
        std::error_code ec;
        std::filesystem::remove(mConfigPath, ec);
    }

    NullBackendInterpreter(const NullBackendInterpreter&) = delete;
    NullBackendInterpreter& operator=(const NullBackendInterpreter&) = delete;

    void Run(Gfx* commands) {
        interpreter->Run(commands, {});
    }

    NullGfxRenderingAPI rapi;
    NullGfxWindowBackend wapi;
    std::shared_ptr<Ship::ConsoleVariable> cvars;
    std::shared_ptr<Interpreter> interpreter;

  private:
    std::string mConfigPath;
    std::shared_ptr<Ship::Config> mConfig;
};

} // namespace Fast
