#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <whb/log.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include "imgui.h"
#include "fast/backends/gfx_gx2.h"
#include "fast/backends/gfx_wiiu.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/port/wiiu/ImGui/imgui_impl_gx2.h"
#include "ship/port/wiiu/WiiUImpl.h"
#include "ship/port/wiiu/WiiUInput.h"

namespace {

void* sScreenBufferTV = nullptr;
void* sScreenBufferDRC = nullptr;

enum class Mode { Menu, BootLink, InputReadout, Audio, Gx2Renderer };

struct MenuItem {
    const char* label;
    Mode mode;
};

constexpr MenuItem kMenuItems[] = {
    { "Stage 0: Boot & Link", Mode::BootLink },
    { "Stage 1: Input Readout", Mode::InputReadout },
    { "Stage 2: AX Audio", Mode::Audio },
    { "Stage 3: GX2 Renderer", Mode::Gx2Renderer },
};
constexpr int kMenuItemCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

// AX audio test tone: a continuous, phase-continuous sweep so a wrap/underrun click
// stands out against an otherwise-smooth pitch change, with the right channel offset
// from the left by a fixed ratio so a channel swap is audible at every point in the
// sweep (a single steady tone can't reveal either of those).
constexpr double kSweepMinHz = 220.0;
constexpr double kSweepMaxHz = 880.0;
constexpr double kSweepPeriodSeconds = 4.0;
constexpr double kRightChannelRatio = 1.25; // Perfect fourth above the left channel.
constexpr int16_t kAmplitude = 8000;
constexpr double kPi = 3.14159265358979323846;

// Mirrors WiiUAudioPlayer's internal ring size (gRingSamples in WiiUAudioPlayer.cpp).
// Buffered() approaching this means the writer is close to lapping the AX read head.
constexpr int32_t kRingSamples = 8192;
constexpr int32_t kHighBufferedThreshold = (kRingSamples * 9) / 10;

// Which channel(s) currently hear the sweep. WiiUAudioPlayer only ever wires two AX
// voices to the front L/R bus (see its class doc — AX can drive 5.1, but the surround
// path isn't wired up here), so Both/Left/Right is the whole space worth isolating.
enum class ChannelMode { Both, Left, Right };

const char* ChannelModeName(ChannelMode mode) {
    switch (mode) {
        case ChannelMode::Both:
            return "BOTH";
        case ChannelMode::Left:
            return "LEFT only";
        case ChannelMode::Right:
            return "RIGHT only";
    }
    return "";
}

ChannelMode NextChannelMode(ChannelMode mode) {
    switch (mode) {
        case ChannelMode::Both:
            return ChannelMode::Left;
        case ChannelMode::Left:
            return ChannelMode::Right;
        case ChannelMode::Right:
            return ChannelMode::Both;
    }
    return ChannelMode::Both;
}

struct AudioTestState {
    std::unique_ptr<Ship::WiiUAudioPlayer> player;
    double phaseL = 0.0;
    double phaseR = 0.0;
    uint64_t sampleIndex = 0;
    uint32_t underrunCount = 0;
    int32_t minBuffered = INT32_MAX;
    int32_t maxBuffered = 0;
    bool highBufferedFlag = false;
    ChannelMode channelMode = ChannelMode::Both;
};

void PrintLine(OSScreenID screen, int row, const std::string& text) {
    OSScreenPutFontEx(screen, 0, row, text.c_str());
}

void PrintBoth(int row, const std::string& text) {
    PrintLine(SCREEN_TV, row, text);
    PrintLine(SCREEN_DRC, row, text);
}

// Creates sd:/wiiu/apps/lus-harness/, tolerating segments that already exist,
// and returns the path to results.txt inside it, or an empty string if the
// SD card isn't mounted.
std::string ResultsFilePath() {
    if (!WHBMountSdCard()) {
        return "";
    }

    const std::string base = std::string(WHBGetSdCardMountPath()) + "wiiu";
    const char* dirs[] = { "", "/apps", "/apps/lus-harness" };
    std::string path = base;
    mkdir(path.c_str(), 0777);
    for (size_t i = 1; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        path = base + dirs[i];
        mkdir(path.c_str(), 0777);
    }

    return base + "/apps/lus-harness/results.txt";
}

// Free bytes remaining in the MEM2-backed default heap, as a coarse sanity
// check that the console has usable memory left after startup.
uint32_t DefaultHeapFreeBytes() {
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
    if (!heap) {
        return 0;
    }
    return MEMGetTotalFreeSizeForExpHeap(heap);
}

// Space-joined names of every normalized button set in a held mask, e.g. "A B D-Pad Up".
std::string DescribeButtonsHeld(int32_t deviceIndex, uint32_t held) {
    std::string out;
    for (uint32_t bit = 0; bit < Ship::WiiU::WIIU_BUTTON_COUNT; bit++) {
        const uint32_t mask = 1u << bit;
        if (!(held & mask)) {
            continue;
        }
        const std::string name = Ship::WiiU::GetButtonName(deviceIndex, mask);
        if (name.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " ";
        }
        out += name;
    }
    return out.empty() ? "(none)" : out;
}

std::string DescribeAxes(int32_t deviceIndex) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "LX:%+.2f LY:%+.2f RX:%+.2f RY:%+.2f",
                  Ship::WiiU::GetAxisValue(deviceIndex, Ship::WiiU::WIIU_AXIS_LEFT_X),
                  Ship::WiiU::GetAxisValue(deviceIndex, Ship::WiiU::WIIU_AXIS_LEFT_Y),
                  Ship::WiiU::GetAxisValue(deviceIndex, Ship::WiiU::WIIU_AXIS_RIGHT_X),
                  Ship::WiiU::GetAxisValue(deviceIndex, Ship::WiiU::WIIU_AXIS_RIGHT_Y));
    return buf;
}

// Writes a snapshot of the boot & link checks to results.txt.
void WriteBootLinkResults(const std::string& resultsPath, const std::string& heapLine) {
    FILE* f = std::fopen(resultsPath.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "libultraship Wii U harness - Stage 0: boot & link\n");
    std::fprintf(f, "compiler: %s\n", __VERSION__);
    std::fprintf(f, "built: %s %s\n", __DATE__, __TIME__);
    std::fprintf(f, "%s\n", heapLine.c_str());
    std::fprintf(f, "sd write: PASS (%s)\n", resultsPath.c_str());
    std::fprintf(f, "stage 0: PASS\n");
    std::fclose(f);
}

// Writes a snapshot of every connected device's current input state to results.txt.
void WriteInputResults(const std::string& resultsPath) {
    FILE* f = std::fopen(resultsPath.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "libultraship Wii U harness - Stage 1: input readout\n");

    const std::vector<int32_t> devices = Ship::WiiU::GetConnectedDeviceIndices();
    if (devices.empty()) {
        std::fprintf(f, "no controllers connected\n");
    }
    for (int32_t deviceIndex : devices) {
        const uint32_t held = Ship::WiiU::GetButtonsHeld(deviceIndex);
        std::fprintf(f, "[%d] %s\n", deviceIndex, Ship::WiiU::GetDeviceName(deviceIndex).c_str());
        std::fprintf(f, "    buttons: %s\n", DescribeButtonsHeld(deviceIndex, held).c_str());
        std::fprintf(f, "    axes: %s\n", DescribeAxes(deviceIndex).c_str());
    }
    std::fclose(f);
}

// Writes a snapshot of the AX audio test's buffering stats to results.txt.
void WriteAudioResults(const std::string& resultsPath, const AudioTestState& state) {
    FILE* f = std::fopen(resultsPath.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "libultraship Wii U harness - Stage 2: AX audio\n");
    if (!state.player) {
        std::fprintf(f, "audio player failed to initialize\n");
        std::fclose(f);
        return;
    }
    std::fprintf(f, "channel: %s\n", ChannelModeName(state.channelMode));
    std::fprintf(f, "buffered: %d (min %d / max %d)\n", state.player->Buffered(), state.minBuffered, state.maxBuffered);
    std::fprintf(f, "underruns: %u\n", state.underrunCount);
    std::fprintf(f, "high buffered (near ring capacity): %s\n", state.highBufferedFlag ? "yes" : "no");
    std::fclose(f);
}

void RenderMenu(int cursor) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "select a stage to test:");
    PrintBoth(row++, "");
    for (int i = 0; i < kMenuItemCount; i++) {
        PrintBoth(row++, std::string(i == cursor ? "> " : "  ") + kMenuItems[i].label);
    }
    PrintBoth(row++, "");
    PrintBoth(row++, "D-Pad Up/Down to move, A to select, HOME to exit.");
}

void RenderBootLink(const std::string& resultsPath, const std::string& heapLine, bool sdWriteOk) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "Stage 0: boot & link");
    PrintBoth(row++, "");
    PrintBoth(row++, std::string("compiler: ") + __VERSION__);
    PrintBoth(row++, std::string("built: ") + __DATE__ + " " + __TIME__);
    PrintBoth(row++, heapLine);
    PrintBoth(row++, "");
    PrintBoth(row++, sdWriteOk ? "sd write: PASS" : "sd write: FAIL (no SD card?)");
    PrintBoth(row++, sdWriteOk ? ("results: " + resultsPath) : "");
    PrintBoth(row++, "");
    PrintBoth(row++, "libultraship.a linked OK (you are looking at proof)");
    PrintBoth(row++, "");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

void RenderInputReadout() {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "Stage 1: input readout");
    PrintBoth(row++, "");

    const std::vector<int32_t> devices = Ship::WiiU::GetConnectedDeviceIndices();
    if (devices.empty()) {
        PrintBoth(row++, "No controllers connected.");
    }
    for (int32_t deviceIndex : devices) {
        const uint32_t held = Ship::WiiU::GetButtonsHeld(deviceIndex);
        PrintBoth(row++, Ship::WiiU::GetDeviceName(deviceIndex));
        PrintBoth(row++, "  " + DescribeButtonsHeld(deviceIndex, held));
        PrintBoth(row++, "  " + DescribeAxes(deviceIndex));
    }

    PrintBoth(row++, "");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

void RenderAudioTest(const AudioTestState& state) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "Stage 2: AX audio");
    PrintBoth(row++, "");

    if (!state.player) {
        PrintBoth(row++, "audio player failed to initialize");
    } else {
        PrintBoth(row++, std::string("channel: ") + ChannelModeName(state.channelMode) + " (X to cycle)");
        PrintBoth(row++, "");

        char buf[64];
        std::snprintf(buf, sizeof(buf), "buffered: %d (min %d / max %d)", state.player->Buffered(), state.minBuffered,
                      state.maxBuffered);
        PrintBoth(row++, buf);

        std::snprintf(buf, sizeof(buf), "underruns: %u", state.underrunCount);
        PrintBoth(row++, state.underrunCount > 0 ? (std::string("*** ") + buf + " ***") : std::string(buf));

        if (state.highBufferedFlag) {
            PrintBoth(row++, "*** buffered approached ring capacity ***");
        }

        PrintBoth(row++, "");
        PrintBoth(row++, "listen for a smooth rising/falling tone.");
        PrintBoth(row++, "left/right pitch should differ throughout.");
        PrintBoth(row++, "any pop/click/dropout = underrun or wrap bug.");
    }

    PrintBoth(row++, "");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

void StartAudioTest(AudioTestState& state) {
    Ship::AudioSettings settings;
    state.player = std::make_unique<Ship::WiiUAudioPlayer>(settings);
    if (!state.player->Init()) {
        state.player.reset();
        return;
    }
    state.phaseL = 0.0;
    state.phaseR = 0.0;
    state.sampleIndex = 0;
    state.underrunCount = 0;
    state.minBuffered = INT32_MAX;
    state.maxBuffered = 0;
    state.highBufferedFlag = false;
    state.channelMode = ChannelMode::Both;
}

void StopAudioTest(AudioTestState& state) {
    state.player.reset(); // ~WiiUAudioPlayer tears the AX voices and ring buffers back down.
}

// Feeds the ring buffer up to its desired-buffered target with fresh sweep samples, then
// updates the buffering stats used by RenderAudioTest()/WriteAudioResults().
void PumpAudioTest(AudioTestState& state) {
    if (!state.player) {
        return;
    }
    Ship::WiiUAudioPlayer& player = *state.player;
    const int32_t sampleRate = player.GetSampleRate();
    const int32_t sampleLength = player.GetSampleLength();
    const int32_t desired = player.GetDesiredBuffered();

    // Bounded so a stalled AX read head can't turn this into a busy-loop.
    int guard = 16;
    while (player.Buffered() < desired && guard-- > 0) {
        std::vector<int16_t> chunk(static_cast<size_t>(sampleLength) * 2);
        for (int32_t i = 0; i < sampleLength; i++) {
            const double t = static_cast<double>(state.sampleIndex) / sampleRate;
            const double freqL =
                kSweepMinHz + (kSweepMaxHz - kSweepMinHz) * 0.5 * (1.0 - std::cos(2.0 * kPi * t / kSweepPeriodSeconds));
            const double freqR = freqL * kRightChannelRatio;

            state.phaseL += 2.0 * kPi * freqL / sampleRate;
            state.phaseR += 2.0 * kPi * freqR / sampleRate;
            if (state.phaseL > 2.0 * kPi) {
                state.phaseL -= 2.0 * kPi;
            }
            if (state.phaseR > 2.0 * kPi) {
                state.phaseR -= 2.0 * kPi;
            }

            // Phases keep advancing regardless of channelMode, so muting/unmuting a
            // channel never introduces a phase discontinuity (and thus a spurious click).
            chunk[i * 2 + 0] = (state.channelMode == ChannelMode::Right)
                                   ? int16_t(0)
                                   : static_cast<int16_t>(std::sin(state.phaseL) * kAmplitude);
            chunk[i * 2 + 1] = (state.channelMode == ChannelMode::Left)
                                   ? int16_t(0)
                                   : static_cast<int16_t>(std::sin(state.phaseR) * kAmplitude);
            state.sampleIndex++;
        }
        player.Play(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size() * sizeof(int16_t));
    }

    const int32_t buffered = player.Buffered();
    if (buffered == 0) {
        state.underrunCount++;
    }
    if (buffered < state.minBuffered) {
        state.minBuffered = buffered;
    }
    if (buffered > state.maxBuffered) {
        state.maxBuffered = buffered;
    }
    if (buffered > kHighBufferedThreshold) {
        state.highBufferedFlag = true;
    }
}

// --- Stage 3: GX2 renderer ---------------------------------------------------------------
//
// Unlike Stages 0-2, this drives Fast::GfxRenderingAPIGX2 directly instead of talking to the
// raw Wii U SDK, on purpose: that's the layer a real N64 decomp actually calls (through the F3D
// microcode interpreter), so it's the layer worth de-risking before one gets ported here. There
// is no display list here, so shader IDs are hand-encoded the same way gfx_cc_get_features()
// would decode them from one (see include/fast/interpreter.h's CCFeatures/SHADER_* enum).
//
// GX2 also can't share the screen with OSScreen (both fight over the TV/DRC scan buffers), so
// entering this stage is a one-way trip for the run: OSScreen stays torn down and an ImGui
// overlay (using ImGui's built-in font, not Fast3dGui/OTR) replaces it. B exits the harness
// instead of returning to the menu, and WHBLogUdp (already live in debug builds via
// Ship::WiiU::Init) replaces on-screen text for anything not shown by the overlay.

// SHADER_INPUT_1 / SHADER_TEXEL0 from the SHADER_* enum in include/fast/interpreter.h.
// Not included directly: interpreter.h pulls in the whole OTR resource/texture-cache
// machinery, which this stage deliberately stays independent of.
constexpr uint64_t kShaderInput1 = 1;
constexpr uint64_t kShaderTexel0 = 8;

// Packs one CCFeatures combiner slot into a shaderId0, matching gfx_cc_get_features()'s
// decode: cc_features->c[i][j][k] = shader_id0 >> (i*32 + j*16 + k*4) & 0xf.
// i: 0 = color cycle, 1 = alpha cycle. j: cycle 0/1 (only cycle 0 is used here). k: A,B,C,D.
constexpr uint64_t PackCCSlot(int i, int j, int k, uint64_t value) {
    return value << (i * 32 + j * 16 + k * 4);
}

// D-only (A=B=C=SHADER_0) on both cycle-0 combiner stages: (A-B)*C+D reduces to a passthrough
// of D, so this makes a shader whose single "attribute" ends up as the final color untouched.
constexpr uint64_t ShaderIdForPassthrough(uint64_t d) {
    return PackCCSlot(0, 0, 3, d) | PackCCSlot(1, 0, 3, d);
}

// Untextured, per-vertex-colored, no fog/lighting: color and alpha both pass through aInput1.
constexpr uint64_t kCubeShaderId0 = ShaderIdForPassthrough(kShaderInput1);
constexpr uint64_t kCubeShaderId1 = 0;

// Textured, no vertex color: color and alpha both pass through the sampled texel.
constexpr uint64_t kQuadShaderId0 = ShaderIdForPassthrough(kShaderTexel0);
constexpr uint64_t kQuadShaderId1 = 0;

using Mat4 = std::array<std::array<float, 4>, 4>;

constexpr Mat4 Mat4Identity() {
    return { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
}

Mat4 Mat4Multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[row][k] * b[k][col];
            }
            out[row][col] = sum;
        }
    }
    return out;
}

Mat4 Mat4RotateY(float radians) {
    Mat4 m = Mat4Identity();
    m[0][0] = std::cos(radians);
    m[0][2] = std::sin(radians);
    m[2][0] = -std::sin(radians);
    m[2][2] = std::cos(radians);
    return m;
}

Mat4 Mat4RotateX(float radians) {
    Mat4 m = Mat4Identity();
    m[1][1] = std::cos(radians);
    m[1][2] = -std::sin(radians);
    m[2][1] = std::sin(radians);
    m[2][2] = std::cos(radians);
    return m;
}

// D3D-style projection (depth range 0..1, matching GX2SetViewport's near/far of 0.0f/1.0f).
Mat4 Mat4Perspective(float fovYRadians, float aspect, float zNear, float zFar) {
    Mat4 m{};
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][2] = zFar / (zFar - zNear);
    m[2][3] = -zNear * zFar / (zFar - zNear);
    m[3][2] = 1.0f;
    return m;
}

// GX2's vertex shader does no MVP transform of its own (see generateVertexShader() in
// src/fast/backends/gx2_shader_gen.cpp) - a real display list's CPU-side matrix stack has
// already put vertices in clip space by the time they reach DrawTriangles(), so this harness
// has to do the same multiply itself.
std::array<float, 4> Mat4TransformPoint(const Mat4& m, float x, float y, float z) {
    const std::array<float, 4> v = { x, y, z, 1.0f };
    std::array<float, 4> out{};
    for (int row = 0; row < 4; row++) {
        out[row] = m[row][0] * v[0] + m[row][1] * v[1] + m[row][2] * v[2] + m[row][3] * v[3];
    }
    return out;
}

void AppendVertex(std::vector<float>& out, const Mat4& mvp, float x, float y, float z, float attr0, float attr1,
                  float attr2, float attr3) {
    const std::array<float, 4> clip = Mat4TransformPoint(mvp, x, y, z);
    out.insert(out.end(), { clip[0], clip[1], clip[2], clip[3], attr0, attr1, attr2, attr3 });
}

struct CubeFace {
    std::array<int, 4> cornerIndices; // into kCubeCorners, wound CCW when viewed from outside
    std::array<float, 3> color;
};

constexpr std::array<std::array<float, 3>, 8> kCubeCorners = { {
    { -1, -1, -1 },
    { 1, -1, -1 },
    { 1, 1, -1 },
    { -1, 1, -1 },
    { -1, -1, 1 },
    { 1, -1, 1 },
    { 1, 1, 1 },
    { -1, 1, 1 },
} };

const std::array<CubeFace, 6> kCubeFaces = { {
    { { 0, 1, 2, 3 }, { 1.0f, 0.2f, 0.2f } }, // back  (-Z), red
    { { 5, 4, 7, 6 }, { 0.2f, 1.0f, 0.2f } }, // front (+Z), green
    { { 4, 0, 3, 7 }, { 0.2f, 0.2f, 1.0f } }, // left  (-X), blue
    { { 1, 5, 6, 2 }, { 1.0f, 1.0f, 0.2f } }, // right (+X), yellow
    { { 3, 2, 6, 7 }, { 1.0f, 0.2f, 1.0f } }, // top   (+Y), magenta
    { { 4, 5, 1, 0 }, { 0.2f, 1.0f, 1.0f } }, // bottom(-Y), cyan
} };

// A hand-made 8x8 RGBA8 checkerboard, uploaded once via UploadTexture() - deliberately not an
// OTR-packed asset, since this stage stays independent of the resource/archive pipeline.
constexpr uint32_t kCheckerSize = 8;
std::array<uint8_t, kCheckerSize * kCheckerSize * 4> MakeCheckerTexture() {
    std::array<uint8_t, kCheckerSize * kCheckerSize * 4> pixels{};
    for (uint32_t y = 0; y < kCheckerSize; y++) {
        for (uint32_t x = 0; x < kCheckerSize; x++) {
            const bool light = ((x + y) & 1) == 0;
            uint8_t* p = &pixels[(y * kCheckerSize + x) * 4];
            p[0] = light ? 240 : 40;
            p[1] = light ? 200 : 40;
            p[2] = light ? 40 : 120;
            p[3] = 255;
        }
    }
    return pixels;
}

struct Gx2TestState {
    // Leaked deliberately: GfxWindowBackendWiiU/GfxRenderingAPIGX2 own the GX2 context and
    // scan buffers for the rest of the process's life once constructed, and tearing GX2 down
    // to hand the screen back to OSScreen isn't supported here (see the comment above). The
    // harness exits instead of returning to the menu, so these live until process exit.
    Fast::GfxWindowBackendWiiU* window = nullptr;
    Fast::GfxRenderingAPIGX2* api = nullptr;

    Fast::ShaderProgram* cubeShader = nullptr;
    Fast::ShaderProgram* quadShader = nullptr;
    uint32_t checkerTextureId = 0;
    bool imguiReady = false;

    uint32_t frameCount = 0;
    float cubeAngle = 0.0f;
    bool initFailed = false;
};

void StartGx2Test(Gx2TestState& state) {
    WHBLogPrint("Stage 3: bringing up GfxWindowBackendWiiU + GfxRenderingAPIGX2");

    state.window = new Fast::GfxWindowBackendWiiU(nullptr);
    state.api = new Fast::GfxRenderingAPIGX2();

    state.window->Init("lus-harness", state.api->GetName(), true, WIIU_DEFAULT_FB_WIDTH, WIIU_DEFAULT_FB_HEIGHT, 0, 0);
    state.api->Init();
    state.api->SetViewport(0, 0, WIIU_DEFAULT_FB_WIDTH, WIIU_DEFAULT_FB_HEIGHT);
    state.api->SetScissor(0, 0, WIIU_DEFAULT_FB_WIDTH, WIIU_DEFAULT_FB_HEIGHT);
    state.api->SetDepthTestAndMask(true, true);

    state.cubeShader = state.api->CreateAndLoadNewShader(kCubeShaderId0, kCubeShaderId1);
    state.quadShader = state.api->CreateAndLoadNewShader(kQuadShaderId0, kQuadShaderId1);
    if (!state.cubeShader || !state.quadShader) {
        WHBLogPrint("Stage 3: shader generation FAILED");
        state.initFailed = true;
        return;
    }

    state.checkerTextureId = state.api->NewTexture();
    state.api->SelectTexture(/*tile=*/0, state.checkerTextureId);
    const std::array<uint8_t, kCheckerSize* kCheckerSize* 4> checker = MakeCheckerTexture();
    state.api->UploadTexture(checker.data(), kCheckerSize, kCheckerSize);
    // 0 == G_TX_NOMIRROR|G_TX_WRAP (libultraship/libultra/gbi.h) - passed as a literal so this
    // stage doesn't need to pull in the GBI headers just for two texture-wrap constants.
    state.api->SetSamplerParameters(/*tile=*/0, /*linear_filter=*/false, 0, 0);

    ImGui::CreateContext();
    state.imguiReady = ImGui_ImplGX2_Init();
    if (!state.imguiReady) {
        WHBLogPrint("Stage 3: ImGui_ImplGX2_Init FAILED (continuing without the overlay)");
    }

    WHBLogPrint("Stage 3: init OK");
}

void StopGx2Test(Gx2TestState& state) {
    if (state.imguiReady) {
        ImGui_ImplGX2_Shutdown();
        ImGui::DestroyContext();
    }
    // state.window / state.api are intentionally not deleted - see the comment on Gx2TestState.
}

void PumpAndRenderGx2Test(Gx2TestState& state, const std::string& resultsPath, int& periodicResultsCounter) {
    if (state.initFailed) {
        return;
    }

    state.window->HandleEvents();
    state.frameCount++;
    state.cubeAngle += 0.02f;

    // Cycling clear color: exercises GfxRenderingAPIGX2::ClearFramebuffer() with real varying
    // input every frame, rather than the single hardcoded black it had before this stage.
    const float t = static_cast<float>(state.frameCount) * 0.01f;
    state.api->SetClearColor(0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::sin(t + 2.094f),
                             0.5f + 0.5f * std::sin(t + 4.188f), 1.0f);

    state.api->StartFrame();
    state.api->StartDrawToFramebuffer(/*fbId=*/0, /*noiseScale=*/1.0f);
    state.api->ClearFramebuffer(/*color=*/true, /*depth=*/true);

    // zFar kept close (20, not 100) so the cube/quad's z=4..6 view-space range doesn't get
    // squeezed into the last sliver of the [0,1] depth buffer.
    const Mat4 projection = Mat4Perspective(60.0f * (3.14159265f / 180.0f),
                                            (float)WIIU_DEFAULT_FB_WIDTH / (float)WIIU_DEFAULT_FB_HEIGHT, 0.1f, 20.0f);
    Mat4 view = Mat4Identity();
    view[2][3] = 5.0f; // push the scene 5 units down +Z (view space) in front of the camera

    // Rotating, untextured, per-vertex-colored cube - exercises CreateAndLoadNewShader's
    // vertex-color path and DrawTriangles' vertex submission.
    {
        const Mat4 model = Mat4Multiply(Mat4RotateY(state.cubeAngle), Mat4RotateX(state.cubeAngle * 0.7f));
        const Mat4 mvp = Mat4Multiply(projection, Mat4Multiply(view, model));

        std::vector<float> vbo;
        vbo.reserve(kCubeFaces.size() * 6 * 8);
        for (const CubeFace& face : kCubeFaces) {
            const auto& c0 = kCubeCorners[face.cornerIndices[0]];
            const auto& c1 = kCubeCorners[face.cornerIndices[1]];
            const auto& c2 = kCubeCorners[face.cornerIndices[2]];
            const auto& c3 = kCubeCorners[face.cornerIndices[3]];
            const auto AppendCorner = [&](const std::array<float, 3>& corner) {
                AppendVertex(vbo, mvp, corner[0], corner[1], corner[2], face.color[0], face.color[1], face.color[2],
                             1.0f);
            };
            AppendCorner(c0);
            AppendCorner(c1);
            AppendCorner(c2);
            AppendCorner(c0);
            AppendCorner(c2);
            AppendCorner(c3);
        }

        state.api->LoadShader(state.cubeShader);
        state.api->DrawTriangles(vbo.data(), vbo.size(), vbo.size() / 8 / 3);
    }

    // Static textured quad below the cube - exercises NewTexture/SelectTexture/UploadTexture/
    // SetSamplerParameters and the textured shader path, independent of vertex color.
    {
        const Mat4 mvp = Mat4Multiply(projection, view);
        std::vector<float> vbo;
        AppendVertex(vbo, mvp, -1.5f, -2.2f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        AppendVertex(vbo, mvp, 1.5f, -2.2f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        AppendVertex(vbo, mvp, 1.5f, -2.2f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
        AppendVertex(vbo, mvp, -1.5f, -2.2f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        AppendVertex(vbo, mvp, 1.5f, -2.2f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
        AppendVertex(vbo, mvp, -1.5f, -2.2f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);

        state.api->LoadShader(state.quadShader);
        state.api->SelectTexture(/*tile=*/0, state.checkerTextureId);
        state.api->DrawTriangles(vbo.data(), vbo.size(), vbo.size() / 8 / 3);
    }

    if (state.imguiReady) {
        ImGui_ImplGX2_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)WIIU_DEFAULT_FB_WIDTH, (float)WIIU_DEFAULT_FB_HEIGHT);
        io.DeltaTime = std::max(frametime / 1000000.0f, 1.0f / 1000.0f);
        ImGui::NewFrame();
        ImGui::Begin("libultraship Wii U harness - Stage 3");
        ImGui::Text("frame: %u", state.frameCount);
        ImGui::Text("cube shader: %s", state.cubeShader ? "OK" : "FAILED");
        ImGui::Text("quad shader: %s", state.quadShader ? "OK" : "FAILED");
        ImGui::Text("checker texture id: %u", state.checkerTextureId);
        ImGui::Text("press B to exit (no return to menu once GX2 has taken the screen)");
        ImGui::End();
        ImGui::ShowDemoWindow();
        ImGui::Render();
        ImGui_ImplGX2_RenderDrawData(ImGui::GetDrawData());
    }

    state.api->EndFrame();
    state.window->SwapBuffersBegin();
    state.api->FinishRender();
    state.window->SwapBuffersEnd();

    if (!resultsPath.empty() && periodicResultsCounter-- <= 0) {
        FILE* f = std::fopen(resultsPath.c_str(), "w");
        if (f != nullptr) {
            std::fprintf(f, "libultraship Wii U harness - Stage 3: GX2 renderer\n");
            std::fprintf(f, "frame: %u\n", state.frameCount);
            std::fprintf(f, "cube shader: %s\n", state.cubeShader ? "OK" : "FAILED");
            std::fprintf(f, "quad shader: %s\n", state.quadShader ? "OK" : "FAILED");
            std::fprintf(f, "checker texture id: %u\n", state.checkerTextureId);
            std::fprintf(f, "imgui overlay: %s\n", state.imguiReady ? "OK" : "FAILED");
            std::fclose(f);
        }
        periodicResultsCounter = 30; // roughly once a second at 60fps-ish
    }
}

} // namespace

int main(int argc, char** argv) {
    WHBProcInit();

    OSScreenInit();
    uint32_t bufferSizeTV = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t bufferSizeDRC = OSScreenGetBufferSizeEx(SCREEN_DRC);
    sScreenBufferTV = MEMAllocFromDefaultHeapEx(bufferSizeTV, 4);
    sScreenBufferDRC = MEMAllocFromDefaultHeapEx(bufferSizeDRC, 4);
    OSScreenSetBufferEx(SCREEN_TV, sScreenBufferTV);
    OSScreenSetBufferEx(SCREEN_DRC, sScreenBufferDRC);
    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);

    Ship::WiiU::Init("lus-harness");

    const std::string resultsPath = ResultsFilePath();
    const bool sdWriteOk = !resultsPath.empty();

    char heapLineBuf[64];
    std::snprintf(heapLineBuf, sizeof(heapLineBuf), "MEM2 heap free: %u bytes", DefaultHeapFreeBytes());
    const std::string heapLine = heapLineBuf;

    if (sdWriteOk) {
        WriteBootLinkResults(resultsPath, heapLine);
    }

    Mode mode = Mode::Menu;
    int menuCursor = 0;
    uint32_t prevGamePadHeld = 0;
    int periodicResultsCounter = 0;
    AudioTestState audioTestState;
    Gx2TestState gx2TestState;
    bool quitRequested = false;

    while (WHBProcIsRunning() && !quitRequested) {
        Ship::WiiU::Update();

        const uint32_t gamePadHeld = Ship::WiiU::GetButtonsHeld(WIIU_DEVICE_GAMEPAD);
        const uint32_t pressed = gamePadHeld & ~prevGamePadHeld;
        prevGamePadHeld = gamePadHeld;

        if (mode == Mode::Menu) {
            if (pressed & Ship::WiiU::WIIU_BUTTON_UP) {
                menuCursor = (menuCursor - 1 + kMenuItemCount) % kMenuItemCount;
            }
            if (pressed & Ship::WiiU::WIIU_BUTTON_DOWN) {
                menuCursor = (menuCursor + 1) % kMenuItemCount;
            }
            if (pressed & Ship::WiiU::WIIU_BUTTON_A) {
                mode = kMenuItems[menuCursor].mode;
                periodicResultsCounter = 0;
                if (mode == Mode::Audio) {
                    StartAudioTest(audioTestState);
                } else if (mode == Mode::Gx2Renderer) {
                    StartGx2Test(gx2TestState);
                }
            }
        } else if (mode == Mode::Gx2Renderer) {
            // GX2 has taken the screen from OSScreen for the rest of this run (see the comment
            // above Gx2TestState) - B exits the harness instead of returning to the menu.
            if (pressed & Ship::WiiU::WIIU_BUTTON_B) {
                StopGx2Test(gx2TestState);
                quitRequested = true;
                continue; // skip PumpAndRenderGx2Test below - ImGui was just torn down
            }
        } else if (pressed & Ship::WiiU::WIIU_BUTTON_B) {
            if (mode == Mode::Audio) {
                StopAudioTest(audioTestState);
            }
            mode = Mode::Menu;
        } else if (mode == Mode::Audio && (pressed & Ship::WiiU::WIIU_BUTTON_X)) {
            audioTestState.channelMode = NextChannelMode(audioTestState.channelMode);
        }

        if (mode == Mode::Gx2Renderer) {
            // Own frame loop: OSScreen must not touch the screen while GX2 owns it.
            PumpAndRenderGx2Test(gx2TestState, resultsPath, periodicResultsCounter);
            continue;
        }

        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        switch (mode) {
            case Mode::Menu:
                RenderMenu(menuCursor);
                break;
            case Mode::BootLink:
                RenderBootLink(resultsPath, heapLine, sdWriteOk);
                break;
            case Mode::InputReadout:
                RenderInputReadout();
                if (sdWriteOk && periodicResultsCounter-- <= 0) {
                    WriteInputResults(resultsPath);
                    periodicResultsCounter = 30; // roughly once a second at 33ms/frame
                }
                break;
            case Mode::Audio:
                PumpAudioTest(audioTestState);
                RenderAudioTest(audioTestState);
                if (sdWriteOk && periodicResultsCounter-- <= 0) {
                    WriteAudioResults(resultsPath, audioTestState);
                    periodicResultsCounter = 30; // roughly once a second at 33ms/frame
                }
                break;
            case Mode::Gx2Renderer:
                break; // handled above, before OSScreen touches the buffers
        }

        DCFlushRange(sScreenBufferTV, bufferSizeTV);
        DCFlushRange(sScreenBufferDRC, bufferSizeDRC);
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);

        OSSleepTicks(OSMillisecondsToTicks(33));
    }

    if (audioTestState.player) {
        StopAudioTest(audioTestState);
    }

    Ship::WiiU::Exit();

    MEMFreeToDefaultHeap(sScreenBufferTV);
    MEMFreeToDefaultHeap(sScreenBufferDRC);
    WHBUnmountSdCard();
    WHBProcShutdown();
    return 0;
}
