#include <algorithm>
#include <array>
#include <atomic>
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

// libultraship/libultra/controller.h (pulled in by ControlDeck.h below) unconditionally trails
// with #include "os.h", which redefines OSThread/OSTime - both already defined by the real Wii U
// SDK's <coreinit/thread.h>/<coreinit/time.h> included above (for the atomics stress test and
// OSSleepTicks/OSMillisecondsToTicks). Nothing this harness needs from that chain - just
// OSContPad and the BTN_* macros, both declared in controller.h before its os.h include - depends
// on anything os.h itself provides, so pre-defining its include guard here skips that unrelated
// (and, in a translation unit that also wants real coreinit types, actively conflicting) content
// without touching the shared header. No other Wii U translation unit combines both header
// families in one file, so this collision never surfaced before this harness.
#define OS_H
#include "libultraship/controller/controldeck/ControlDeck.h"
#include "libultraship/libultra/controller.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/controller/controldevice/controller/ControllerRumble.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUMapping.h"
#include "ship/controller/physicaldevice/PhysicalDeviceType.h"
#include "ship/core/Context.h"
#include "ship/port/wiiu/ImGui/imgui_impl_gx2.h"
#include "ship/port/wiiu/WiiUImpl.h"
#include "ship/port/wiiu/WiiUInput.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"
#include "ship/thread/ThreadPool.h"

// Neither of these touch os.h (they only pull in mbi.h/types.h), so they're safe outside the
// OS_H guard above - this is the same real GBI a decomp uses to author display lists: Vtx, Gfx,
// the gsSP*/gsDP* macros, and Mtx (the fixed-point matrix format libultra's gu*() helpers write).
#include "fast/interpreter.h"
#include "libultraship/libultra/gbi.h"
#include "libultraship/libultra/gu.h"

#include "HarnessWindow.h"

namespace {

void* sScreenBufferTV = nullptr;
void* sScreenBufferDRC = nullptr;

// Mirrors libultraship's MAXCONTROLLERS (include/libultraship/libultra/os.h), which this file
// can't include directly - see the OS_H comment above the includes block. This fork always
// builds with the non-_HW_VERSION_1 (N64DD) value, so 4 is the only value that macro ever takes.
constexpr uint8_t kControllerPortCount = 4;

enum class Mode { Menu, BootLink, InputReadout, InputMapped, Audio, Gx2Renderer, FullContext };

struct MenuItem {
    const char* label;
    Mode mode;
};

constexpr MenuItem kMenuItems[] = {
    { "Core: Boot & Link", Mode::BootLink },
    { "Input: Raw Readout", Mode::InputReadout },
    { "Input: ControlDeck Mapping", Mode::InputMapped },
    { "Audio: Manager Playback", Mode::Audio },
    { "Graphics: GX2 Renderer", Mode::Gx2Renderer },
    { "Graphics: Context + Display List", Mode::FullContext },
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
    // Config backing the Audio manager below; only needs a writable path, no OTR/ArchiveManager.
    std::shared_ptr<Ship::Config> config;
    // The actual interface a decomp/port (e.g. Ship of Harkinian) calls: Context::GetAudio() in
    // a full game, selecting/initialising the backend (AX here) and letting the channel mode be
    // switched at runtime. Testing through this instead of constructing WiiUAudioPlayer directly
    // exercises that selection path too (see issue #14).
    std::shared_ptr<Ship::Audio> audioManager;
    std::shared_ptr<Ship::AudioPlayer> player;
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

// Creates sd:/wiiu/apps/lus-harness/, tolerating segments that already exist, and returns its
// path, or an empty string if the SD card isn't mounted.
std::string HarnessDirPath() {
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

    return path;
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

// Which player(s) Ship::WiiUDefaultDevicesForPort() would bind this device to by default -
// the same, already-shipped policy the real ControlDeck mapping layer uses (port 0 gets both
// the GamePad and KPAD channel 0, so they share "P1"). This is what makes a device's "player"
// meaningful for multiplayer, rather than a harness-invented numbering.
std::string DescribeDefaultPlayerSlot(int32_t deviceIndex) {
    std::string out;
    for (uint8_t port = 0; port < kControllerPortCount; port++) {
        for (int32_t candidate : Ship::WiiUDefaultDevicesForPort(port)) {
            if (candidate == deviceIndex) {
                if (!out.empty()) {
                    out += "/";
                }
                out += "P" + std::to_string(port + 1);
            }
        }
    }
    return out.empty() ? "no default port" : out;
}

// The device(s) Ship::WiiUDefaultDevicesForPort() binds to a given port by default, joined for
// display (e.g. port 0 -> "Wii U GamePad + Wii U Controller 1 (disconnected)").
std::string DescribePortDevices(uint8_t port) {
    std::string out;
    for (int32_t deviceIndex : Ship::WiiUDefaultDevicesForPort(port)) {
        if (!out.empty()) {
            out += " + ";
        }
        out += Ship::WiiU::GetDeviceName(deviceIndex);
    }
    return out;
}

// Decodes an OSContPad the way a real N64 decomp would read it: button names for the bits
// AddDefaultMappings(PHYSICAL_DEVICE_TYPE_GAMEPAD) actually wires up (see
// ControllerDefaultMappings::SetDefaultWiiU*Mappings), plus the analog stick and the fork's
// bonus right_stick_x/y pair (the default mapping also derives the digital BTN_C* bits from the
// same right-stick tilt, so both should move together).
std::string DescribeOSContPad(const OSContPad& pad) {
    struct BitName {
        uint16_t bit;
        const char* name;
    };
    constexpr BitName kBits[] = {
        { BTN_A, "A" },         { BTN_B, "B" },           { BTN_Z, "Z" },     { BTN_L, "L" },
        { BTN_R, "R" },         { BTN_START, "Start" },   { BTN_DUP, "DUp" }, { BTN_DDOWN, "DDown" },
        { BTN_DLEFT, "DLeft" }, { BTN_DRIGHT, "DRight" }, { BTN_CUP, "CUp" }, { BTN_CDOWN, "CDown" },
        { BTN_CLEFT, "CLeft" }, { BTN_CRIGHT, "CRight" },
    };

    std::string buttons;
    for (const auto& entry : kBits) {
        if (pad.button & entry.bit) {
            if (!buttons.empty()) {
                buttons += " ";
            }
            buttons += entry.name;
        }
    }
    if (buttons.empty()) {
        buttons = "(none)";
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), " stick:(%d,%d) rstick:(%d,%d)", pad.stick_x, pad.stick_y, pad.right_stick_x,
                  pad.right_stick_y);
    return buttons + buf;
}

// Exercises the WiiUAtomics.cpp __atomic_fetch_add_8 shim (see issue #16) under
// genuine cross-core contention: three threads, one pinned to each of the
// Espresso's real cores, all hammering the same std::atomic<uint64_t> counter.
// A broken (unsynchronized) shim loses increments under real contention in a
// way an emulator or single-core run won't reliably reproduce, so this has to
// run on hardware to mean anything.
constexpr uint32_t kAtomicStressThreadCount = 3;
constexpr uint64_t kAtomicStressIncrementsPerThread = 100000;
constexpr uint32_t kAtomicStressStackSize = 16 * 1024;

struct AtomicStressResult {
    bool ran = false;
    uint64_t expected = 0;
    uint64_t actual = 0;
    bool Passed() const {
        return ran && actual == expected;
    }
};

int AtomicStressThreadEntry(int /*argc*/, const char** argv) {
    auto* counter = reinterpret_cast<std::atomic<uint64_t>*>(argv);
    for (uint64_t i = 0; i < kAtomicStressIncrementsPerThread; i++) {
        counter->fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

AtomicStressResult RunAtomicStressTest() {
    static const OSThreadAttributes kCoreAffinity[kAtomicStressThreadCount] = {
        OS_THREAD_ATTRIB_AFFINITY_CPU0,
        OS_THREAD_ATTRIB_AFFINITY_CPU1,
        OS_THREAD_ATTRIB_AFFINITY_CPU2,
    };

    std::atomic<uint64_t> counter{ 0 };
    alignas(8) OSThread threads[kAtomicStressThreadCount];
    void* stacks[kAtomicStressThreadCount] = {};

    uint32_t started = 0;
    for (; started < kAtomicStressThreadCount; started++) {
        void* stackBase = MEMAllocFromDefaultHeapEx(kAtomicStressStackSize, 8);
        if (stackBase == nullptr) {
            break;
        }
        stacks[started] = stackBase;
        void* stackTop = static_cast<uint8_t*>(stackBase) + kAtomicStressStackSize;
        const BOOL created =
            OSCreateThread(&threads[started], AtomicStressThreadEntry, 0, reinterpret_cast<char*>(&counter), stackTop,
                           kAtomicStressStackSize, 16, kCoreAffinity[started]);
        if (!created) {
            MEMFreeToDefaultHeap(stackBase);
            stacks[started] = nullptr;
            break;
        }
        OSResumeThread(&threads[started]);
    }

    for (uint32_t i = 0; i < started; i++) {
        int exitCode = 0;
        OSJoinThread(&threads[i], &exitCode);
        MEMFreeToDefaultHeap(stacks[i]);
    }

    AtomicStressResult result;
    result.ran = (started == kAtomicStressThreadCount);
    result.expected = static_cast<uint64_t>(kAtomicStressThreadCount) * kAtomicStressIncrementsPerThread;
    result.actual = counter.load(std::memory_order_relaxed);
    return result;
}

// Writes a snapshot of the boot & link checks to results.txt.
void WriteBootLinkResults(const std::string& resultsPath, const std::string& heapLine,
                          const AtomicStressResult& atomicStress) {
    FILE* f = std::fopen(resultsPath.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "libultraship Wii U harness - core: boot & link\n");
    std::fprintf(f, "compiler: %s\n", __VERSION__);
    std::fprintf(f, "built: %s %s\n", __DATE__, __TIME__);
    std::fprintf(f, "%s\n", heapLine.c_str());
    std::fprintf(f, "sd write: PASS (%s)\n", resultsPath.c_str());
    if (!atomicStress.ran) {
        std::fprintf(f, "atomics stress (3-core __atomic_fetch_add_8): FAIL (could not start all threads)\n");
    } else {
        std::fprintf(f, "atomics stress (3-core __atomic_fetch_add_8): %s (%llu / %llu increments)\n",
                     atomicStress.Passed() ? "PASS" : "FAIL", static_cast<unsigned long long>(atomicStress.actual),
                     static_cast<unsigned long long>(atomicStress.expected));
    }
    std::fprintf(f, "core: %s\n", atomicStress.Passed() ? "PASS" : "FAIL");
    std::fclose(f);
}

// Writes a snapshot of every connected device's current input state to results.txt.
void WriteInputResults(const std::string& resultsPath) {
    FILE* f = std::fopen(resultsPath.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "libultraship Wii U harness - input: controller readout\n");

    const std::vector<int32_t> devices = Ship::WiiU::GetConnectedDeviceIndices();
    if (devices.empty()) {
        std::fprintf(f, "no controllers connected\n");
    }
    std::fprintf(f, "%zu device(s) connected\n", devices.size());
    for (int32_t deviceIndex : devices) {
        const uint32_t held = Ship::WiiU::GetButtonsHeld(deviceIndex);
        std::fprintf(f, "[%d] %s (default: %s)\n", deviceIndex, Ship::WiiU::GetDeviceName(deviceIndex).c_str(),
                     DescribeDefaultPlayerSlot(deviceIndex).c_str());
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
    std::fprintf(f, "libultraship Wii U harness - audio: manager playback\n");
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
    PrintBoth(row++, "select a category to test:");
    PrintBoth(row++, "");
    for (int i = 0; i < kMenuItemCount; i++) {
        PrintBoth(row++, std::string(i == cursor ? "> " : "  ") + kMenuItems[i].label);
    }
    PrintBoth(row++, "");
    PrintBoth(row++, "D-Pad Up/Down to move, A to select, HOME to exit.");
}

void RenderBootLink(const std::string& resultsPath, const std::string& heapLine, bool sdWriteOk,
                    const AtomicStressResult& atomicStress) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "core: boot & link");
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
    char buf[80];
    if (!atomicStress.ran) {
        PrintBoth(row++, "atomics stress: FAIL (could not start all 3 core threads)");
    } else {
        std::snprintf(buf, sizeof(buf), "atomics stress (3-core): %s (%llu / %llu)",
                      atomicStress.Passed() ? "PASS" : "FAIL", static_cast<unsigned long long>(atomicStress.actual),
                      static_cast<unsigned long long>(atomicStress.expected));
        PrintBoth(row++, buf);
    }
    PrintBoth(row++, "");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

void RenderInputReadout() {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "input: controller readout");
    PrintBoth(row++, "");

    const std::vector<int32_t> devices = Ship::WiiU::GetConnectedDeviceIndices();
    if (devices.empty()) {
        PrintBoth(row++, "No controllers connected.");
    }
    for (int32_t deviceIndex : devices) {
        const uint32_t held = Ship::WiiU::GetButtonsHeld(deviceIndex);
        PrintBoth(row++, Ship::WiiU::GetDeviceName(deviceIndex) + " [" + DescribeDefaultPlayerSlot(deviceIndex) + "]");
        PrintBoth(row++, "  " + DescribeButtonsHeld(deviceIndex, held));
        PrintBoth(row++, "  " + DescribeAxes(deviceIndex));
    }

    PrintBoth(row++, "");
    PrintBoth(row++, "[Px] = default ControlDeck player port (see next category).");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

// --- Input: ControlDeck mapping (the real libultraship path) -----------------------------
//
// The readout above proves Ship::WiiU (raw VPAD/KPAD translation) works, but a real decomp
// never calls that directly - it goes through ControlDeck -> mapping factories ->
// WiiUButtonToButtonMapping/WiiUAxisDirectionToButtonMapping/WiiURumbleMapping, all built from
// WiiUDefaultDevicesForPort() the same way a shipped game's would be (see issue #14). This test
// drives an actual LUS::ControlDeck end to end and decodes the resulting OSContPad per port,
// so a mapping-layer bug can be told apart from a WiiUInput.cpp bug.
//
// ControlDeck's mapping objects dereference mControlDeck->GamepadGameInputBlocked(), which
// needs a live Window+Gui - see HarnessWindow.h for why a stub Window is enough here without
// pulling in the OTR/ResourceManager machinery a full Context would need.
struct ControlDeckTestState {
    std::shared_ptr<Ship::Config> config;
    std::shared_ptr<Ship::ConsoleVariable> consoleVariable;
    std::shared_ptr<HarnessWindow> window;
    std::shared_ptr<LUS::ControlDeck> controlDeck;
    uint8_t controllerBits = 0;
    OSContPad pads[kControllerPortCount] = {};
    bool rumbleHeld[kControllerPortCount] = { false };
};

// Constructed once on first entry and never torn down: Gui's destructor unconditionally tears
// down an ImGui context, which is only meaningful for a Gui that was actually Init()'d (ours
// deliberately isn't - see HarnessWindow.h), and there's no need to rebuild this state on every
// menu visit.
void EnsureControlDeckTest(ControlDeckTestState& state, const std::string& harnessDir) {
    if (state.controlDeck != nullptr) {
        return;
    }

    state.config = std::make_shared<Ship::Config>(harnessDir + "/input-config.json");
    state.consoleVariable = std::make_shared<Ship::ConsoleVariable>(state.config);
    state.window = std::make_shared<HarnessWindow>();
    state.window->Init({});

    state.controlDeck = std::make_shared<LUS::ControlDeck>(state.window, state.consoleVariable);
    state.controlDeck->Init(&state.controllerBits);

    // Init() only seeds port 0 with default mappings when it finds no saved config. Add them to
    // every port unconditionally so every player gets real button/axis/rumble
    // mappings built from WiiUDefaultDevicesForPort(), regardless of what a prior run saved.
    for (uint8_t port = 0; port < kControllerPortCount; port++) {
        state.controlDeck->GetControllerByPort(port)->AddDefaultMappings(PHYSICAL_DEVICE_TYPE_GAMEPAD);
    }
}

void PumpControlDeckTest(ControlDeckTestState& state) {
    if (!state.controlDeck) {
        return; // EnsureControlDeckTest() was never called - no SD card to back Config with.
    }

    std::memset(state.pads, 0, sizeof(state.pads));
    // WriteToOSContPad() is private; WriteToPad() is the public override a real game calls
    // (through the base Ship::ControlDeck interface) and forwards to it internally.
    state.controlDeck->WriteToPad(static_cast<void*>(state.pads));

    // "+" is the one button every Wii U device kind reports, including a bare Wii Remote (which
    // has no Y/X), so it doubles as this test's rumble trigger: hold it on any device bound to a
    // port to drive that port's Controller::GetRumble()->StartRumble()/StopRumble() through the
    // real RumbleMappingFactory-built WiiURumbleMapping fan-out, instead of calling
    // Ship::WiiU::SetRumble() directly.
    for (uint8_t port = 0; port < kControllerPortCount; port++) {
        bool held = false;
        for (int32_t deviceIndex : Ship::WiiUDefaultDevicesForPort(port)) {
            if (Ship::WiiU::GetButtonsHeld(deviceIndex) & Ship::WiiU::WIIU_BUTTON_PLUS) {
                held = true;
                break;
            }
        }
        if (held != state.rumbleHeld[port]) {
            state.rumbleHeld[port] = held;
            std::shared_ptr<Ship::ControllerRumble> rumble = state.controlDeck->GetControllerByPort(port)->GetRumble();
            if (held) {
                rumble->StartRumble();
            } else {
                rumble->StopRumble();
            }
        }
    }
}

void RenderControlDeckTest(const ControlDeckTestState& state) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "input: ControlDeck mapping");
    PrintBoth(row++, "");

    if (!state.controlDeck) {
        PrintBoth(row++, "requires an SD card (Config needs a writable path).");
        PrintBoth(row++, "");
        PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
        return;
    }

    for (uint8_t port = 0; port < kControllerPortCount; port++) {
        PrintBoth(row++, "Player " + std::to_string(port + 1) + " [" + DescribePortDevices(port) + "]");
        PrintBoth(row++, "  " + DescribeOSContPad(state.pads[port]));
    }

    PrintBoth(row++, "");
    PrintBoth(row++, "hold + on a device to rumble its player's port.");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

void WriteControlDeckResults(const std::string& resultsPath, const ControlDeckTestState& state) {
    if (!state.controlDeck) {
        return;
    }

    FILE* f = std::fopen(resultsPath.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "libultraship Wii U harness - input: ControlDeck mapping\n");
    for (uint8_t port = 0; port < kControllerPortCount; port++) {
        std::fprintf(f, "Player %d [%s]\n", port + 1, DescribePortDevices(port).c_str());
        std::fprintf(f, "    %s\n", DescribeOSContPad(state.pads[port]).c_str());
        std::fprintf(f, "    rumble held: %s\n", state.rumbleHeld[port] ? "yes" : "no");
    }
    std::fclose(f);
}

void RenderAudioTest(const AudioTestState& state) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "audio: manager playback");
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

void StartAudioTest(AudioTestState& state, const std::string& harnessDir) {
    state.config = std::make_shared<Ship::Config>(harnessDir + "/audio-config.json");

    Ship::AudioSettings settings;
    state.audioManager = std::make_shared<Ship::Audio>(settings, state.config);
    state.audioManager->Init();
    state.player = state.audioManager->GetAudioPlayer();
    if (!state.player || !state.player->IsInitialized()) {
        state.player.reset();
        state.audioManager.reset();
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
    state.player.reset();
    state.audioManager.reset(); // ~Audio destroys the AudioPlayer, which tears the AX voices down.
}

// Feeds the ring buffer up to its desired-buffered target with fresh sweep samples, then
// updates the buffering stats used by RenderAudioTest()/WriteAudioResults().
void PumpAudioTest(AudioTestState& state) {
    if (!state.player) {
        return;
    }
    Ship::AudioPlayer& player = *state.player;
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

// --- graphics: GX2 renderer ---------------------------------------------------------------
//
// Unlike the categories above, this drives Fast::GfxRenderingAPIGX2 directly instead of talking to the
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
    WHBLogPrint("Graphics: bringing up GfxWindowBackendWiiU + GfxRenderingAPIGX2");

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
        WHBLogPrint("Graphics: shader generation FAILED");
        state.initFailed = true;
        return;
    }

    state.checkerTextureId = state.api->NewTexture();
    state.api->SelectTexture(/*tile=*/0, state.checkerTextureId);
    const std::array<uint8_t, kCheckerSize * kCheckerSize * 4> checker = MakeCheckerTexture();
    state.api->UploadTexture(checker.data(), kCheckerSize, kCheckerSize);
    // 0 == G_TX_NOMIRROR|G_TX_WRAP (libultraship/libultra/gbi.h) - passed as a literal so this
    // stage doesn't need to pull in the GBI headers just for two texture-wrap constants.
    state.api->SetSamplerParameters(/*tile=*/0, /*linear_filter=*/false, 0, 0);

    ImGui::CreateContext();
    state.imguiReady = ImGui_ImplGX2_Init();
    if (!state.imguiReady) {
        WHBLogPrint("Graphics: ImGui_ImplGX2_Init FAILED (continuing without the overlay)");
    }

    WHBLogPrint("Graphics: init OK");
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
        ImGui::Begin("libultraship Wii U harness - Graphics");
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
            std::fprintf(f, "libultraship Wii U harness - graphics: GX2 renderer\n");
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

// --- graphics: Context + display list through the F3D interpreter ------------------------
//
// Mapping (issue #14's ControlDeck-mapping category above) and audio (the Audio manager
// category above) already prove libultraship's own abstractions work on Wii U, so per issue #5
// what's left is the one layer nothing has touched yet: a real Context booting against an
// archive, and a hand-authored Gfx display list running through Fast::Interpreter into
// GfxRenderingAPIGX2 - the same path a decomp's own display lists take, as opposed to the GX2
// renderer category's direct GfxRenderingAPIGX2 calls above with hand-encoded shader IDs.
//
// The Context here is deliberately hand-assembled with just a ResourceManager - not
// Context::CreateDefaultInstance(), which would also pull in Window/Audio/ControlDeck that are
// already covered above - via the lower-level Context::CreateInstance(name, shortName,
// components) overload from Context.h's docs.
//
// ArchiveManager::Init() (see ArchiveManager::AddArchive()) treats any archive path with no file
// extension as a FolderArchive root, so a bare loose-file directory is enough to satisfy
// Context.cpp's ThrowMissingOTR gate - no .o2r/.otr build step needed for this stage. The
// harness creates that directory and its one marker file itself at startup rather than shipping
// it as packaged content, so this test is fully self-contained.

std::string ContextFixtureDir(const std::string& harnessDir) {
    return harnessDir + "/context-assets";
}

bool WriteContextFixture(const std::string& dir) {
    mkdir(dir.c_str(), 0777); // ignore EEXIST - only failure that matters is the write below
    const std::string markerPath = dir + "/harness_marker.txt";
    FILE* f = std::fopen(markerPath.c_str(), "w");
    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "lus-harness context-test fixture - a loose file for FolderArchive to index.\n");
    std::fclose(f);
    return true;
}

struct ContextTestState {
    std::shared_ptr<Ship::Config> config;
    std::shared_ptr<Ship::ConsoleVariable> consoleVariable;
    std::shared_ptr<Ship::ThreadPool> threadPool;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    std::shared_ptr<Ship::Context> context;

    bool contextBootFailed = false;
    bool archiveInitialized = false;
    bool fixtureFileResolved = false;
    // Expected true: this fixture deliberately carries no font resource, so Fast3dGui's
    // OTR-backed font path (out of scope for this stage - see #5's open question) should fail to
    // resolve cleanly rather than hang or crash. Constructing a full Gui to test that path for
    // real is follow-up work, not part of tonight's run.
    bool fontProbeAbsentAsExpected = false;

    // Leaked deliberately, same rationale as Gx2TestState above: GfxWindowBackendWiiU /
    // GfxRenderingAPIGX2 own the GX2 context and scan buffers for the rest of the process's
    // life once constructed, and this category is a one-way trip for the same reason the GX2
    // renderer category above is.
    Fast::GfxWindowBackendWiiU* window = nullptr;
    Fast::GfxRenderingAPIGX2* api = nullptr;
    Fast::Interpreter* interpreter = nullptr;
    bool imguiReady = false;

    uint32_t frameCount = 0;
};

// A single hand-authored display list: the same shape of Gfx[] a real N64 decomp's game code
// would build (SPMatrix/SPViewport/SPVertex/SP1Triangle via the real gsSP*/gsDP* GBI macros),
// run through Fast::Interpreter::Run() the same way the interpreter runs a decomp's own display
// lists - not called directly against GfxRenderingAPIGX2 the way the GX2 renderer category's
// cube/quad above are.
// Vertex colors only (no lighting, no texture) - the smallest list that still exercises
// SPMatrix, SPVertex and SP1Triangle through the real interpreter path end to end.
//
// sContextProjMtx/sContextModelViewMtx/sContextViewport/sContextVerts are filled at runtime (by
// guPerspective()/guMtxIdent() and by hand below) before the first Run() call - only their
// addresses need to be fixed at static-init time, which is what the display list actually
// embeds, exactly like a real decomp's static Gfx arrays referencing separate Mtx globals.
Mtx sContextProjMtx;
Mtx sContextModelViewMtx;
Vp sContextViewport;
Vtx sContextVerts[3];

const Gfx sContextDisplayList[] = {
    gsSPMatrix(&sContextProjMtx, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH),
    gsSPMatrix(&sContextModelViewMtx, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
    gsSPViewport(&sContextViewport),
    gsSPClearGeometryMode(0xFFFFFFFF),
    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),
    gsSPVertex(&sContextVerts[0], 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPEndDisplayList(),
};

void SetContextVertex(Vtx& vtx, int16_t x, int16_t y, int16_t z, uint8_t r, uint8_t g, uint8_t b) {
    vtx.v.ob[0] = x;
    vtx.v.ob[1] = y;
    vtx.v.ob[2] = z;
    vtx.v.flag = 0;
    vtx.v.tc[0] = 0;
    vtx.v.tc[1] = 0;
    vtx.v.cn[0] = r;
    vtx.v.cn[1] = g;
    vtx.v.cn[2] = b;
    vtx.v.cn[3] = 255;
}

// Boots the Context/ResourceManager/ArchiveManager half of the stage. Safe to call every time
// the menu is entered - ResourceManager::Init()/Context::CreateInstance() are only ever run
// once, on the first entry (mirrors EnsureControlDeckTest()'s guard above).
void EnsureContextTest(ContextTestState& state, const std::string& harnessDir) {
    if (state.context != nullptr || state.contextBootFailed) {
        return;
    }

    const std::string fixtureDir = ContextFixtureDir(harnessDir);
    if (!WriteContextFixture(fixtureDir)) {
        WHBLogPrint("Context test: failed to write the FolderArchive fixture (no SD card?)");
        state.contextBootFailed = true;
        return;
    }

    state.config = std::make_shared<Ship::Config>(harnessDir + "/context-config.json");
    state.consoleVariable = std::make_shared<Ship::ConsoleVariable>(state.config);
    state.threadPool = std::make_shared<Ship::ThreadPool>(1);
    state.resourceManager = std::make_shared<Ship::ResourceManager>(state.threadPool);

    try {
        state.resourceManager->Init({ { "archivePaths", std::vector<std::string>{ fixtureDir } } });
    } catch (const std::exception& e) {
        WHBLogPrintf("Context test: ResourceManager::Init threw: %s", e.what());
        state.contextBootFailed = true;
        return;
    }

    state.archiveInitialized = state.resourceManager->GetArchiveManager()->IsInitialized();
    if (!state.archiveInitialized) {
        WHBLogPrint("Context test: ArchiveManager failed to initialize against the fixture");
        state.contextBootFailed = true;
        return;
    }

    // Confirm the loose file is actually resolvable through the ArchiveManager, not just that
    // IsInitialized() went true - that's the real pass condition #5 asks this stage to prove.
    state.fixtureFileResolved = state.resourceManager->GetArchiveManager()->HasFile("harness_marker.txt");

    // This fixture never provides a font resource on purpose (see the comment on
    // fontProbeAbsentAsExpected above) - a miss here is the expected, clean-failure outcome.
    state.fontProbeAbsentAsExpected = !state.resourceManager->GetArchiveManager()->HasFile("textures/font.otr");

    // The lower-level overload (Context.h): adds each component and calls Init() on it in order.
    // ResourceManager::Init() above already ran, and Component::Init() is a no-op past the first
    // call, so this doesn't redo the archive work - it just attaches the already-booted
    // ResourceManager as a real Context child.
    state.context =
        Ship::Context::CreateInstance("lus-harness-context", "lus-harness-context", { state.resourceManager });

    WHBLogPrintf("Context test: Context boot OK (archive: %s, fixture file: %s, font probe: %s)",
                 state.archiveInitialized ? "OK" : "FAILED", state.fixtureFileResolved ? "OK" : "FAILED",
                 state.fontProbeAbsentAsExpected ? "absent as expected" : "unexpectedly present");
}

// OSScreen fallback shown only when EnsureContextTest() fails (no SD card, or the archive/
// Context boot itself failed) - the success path never reaches this, since a successful boot
// immediately hands the screen to GX2 (see the one-way rationale on ContextTestState above).
void RenderContextTestStatus(const ContextTestState& state) {
    int row = 0;
    PrintBoth(row++, "libultraship Wii U harness");
    PrintBoth(row++, "graphics: context + display list");
    PrintBoth(row++, "");
    if (!state.context) {
        PrintBoth(row++, "requires an SD card (Config/fixture need a writable path).");
    } else {
        PrintBoth(row++, "Context/ArchiveManager boot FAILED - see WHBLogUdp / results.txt.");
    }
    PrintBoth(row++, "");
    PrintBoth(row++, "Press B to return to menu. Press HOME to exit.");
}

// Brings up the same GfxWindowBackendWiiU + GfxRenderingAPIGX2 pair the GX2 renderer category
// above uses, but hands frames to a real Fast::Interpreter instead of calling the API directly.
// One-way, like that category: OSScreen and GX2 can't share the display, so B exits the harness
// from here instead of returning to the menu.
void StartContextGraphicsTest(ContextTestState& state) {
    WHBLogPrint("Context test: bringing up GfxWindowBackendWiiU + GfxRenderingAPIGX2 + Interpreter");

    state.window = new Fast::GfxWindowBackendWiiU(nullptr);
    state.api = new Fast::GfxRenderingAPIGX2();
    state.interpreter = new Fast::Interpreter();

    // Interpreter::Init() calls wapi->Init()/rapi->Init() itself (unlike the GX2 renderer
    // category above, which calls them directly) - it owns the window/renderer bring-up once
    // handed to it.
    state.interpreter->Init(state.window, state.api, "lus-harness-context", true, WIIU_DEFAULT_FB_WIDTH,
                            WIIU_DEFAULT_FB_HEIGHT, 0, 0, state.consoleVariable, state.resourceManager);

    // Standard N64 SDK default viewport for a 320x240 native resolution (SCREEN_WIDTH/
    // SCREEN_HEIGHT from include/fast/interpreter.h): half-width/height in 2-bit-fraction fixed
    // point, max Z range, no translation.
    for (int i = 0; i < 3; i++) {
        sContextViewport.vp.vscale[i] = (i == 0) ? (SCREEN_WIDTH / 2) * 4 : (i == 1) ? (SCREEN_HEIGHT / 2) * 4 : G_MAXZ;
        sContextViewport.vp.vtrans[i] = (i == 2) ? G_MAXZ : (i == 0) ? (SCREEN_WIDTH / 2) * 4 : (SCREEN_HEIGHT / 2) * 4;
    }
    sContextViewport.vp.vscale[3] = 0;
    sContextViewport.vp.vtrans[3] = 0;

    guMtxIdent(&sContextModelViewMtx);
    uint16_t perspNorm = 0;
    guPerspective(&sContextProjMtx, &perspNorm, 60.0f, (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, 10.0f, 1000.0f, 1.0f);

    // A simple centered triangle, one primary color per vertex - object-space coordinates chosen
    // to land comfortably inside guPerspective()'s 10..1000 near/far range with an identity
    // modelview, so no separate camera transform is needed for this minimal a scene.
    SetContextVertex(sContextVerts[0], 0, 60, -300, 255, 40, 40);
    SetContextVertex(sContextVerts[1], -70, -60, -300, 40, 255, 40);
    SetContextVertex(sContextVerts[2], 70, -60, -300, 40, 40, 255);

    ImGui::CreateContext();
    state.imguiReady = ImGui_ImplGX2_Init();
    if (!state.imguiReady) {
        WHBLogPrint("Context test: ImGui_ImplGX2_Init FAILED (continuing without the overlay)");
    }

    WHBLogPrint("Context test: graphics init OK");
}

void StopContextGraphicsTest(ContextTestState& state) {
    if (state.imguiReady) {
        ImGui_ImplGX2_Shutdown();
        ImGui::DestroyContext();
    }
    // state.window / state.api / state.interpreter are intentionally not deleted - see the
    // comment on ContextTestState.
}

void PumpAndRenderContextTest(ContextTestState& state, const std::string& resultsPath,
                              int& periodicResultsCounter) {
    state.interpreter->HandleWindowEvents();
    state.frameCount++;

    state.interpreter->StartFrame();
    // Non-const cast: Interpreter::Run() takes Gfx* to match how a decomp hands it a mutable
    // display list pointer (branches/DMA rewrite it in place); this harness's list never
    // mutates itself, so aliasing the const array here is safe.
    state.interpreter->Run(const_cast<Gfx*>(sContextDisplayList), {});

    if (state.imguiReady) {
        ImGui_ImplGX2_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)WIIU_DEFAULT_FB_WIDTH, (float)WIIU_DEFAULT_FB_HEIGHT);
        io.DeltaTime = std::max(frametime / 1000000.0f, 1.0f / 1000.0f);
        ImGui::NewFrame();
        ImGui::Begin("libultraship Wii U harness - Context + Display List");
        ImGui::Text("frame: %u", state.frameCount);
        ImGui::Text("archive init: %s", state.archiveInitialized ? "OK" : "FAILED");
        ImGui::Text("fixture file resolved: %s", state.fixtureFileResolved ? "OK" : "FAILED");
        ImGui::Text("font probe (expect absent): %s", state.fontProbeAbsentAsExpected ? "OK" : "UNEXPECTED");
        ImGui::Text("triangle drawn via Fast::Interpreter::Run(), not direct API calls");
        ImGui::Text("press B to exit (no return to menu once GX2 has taken the screen)");
        ImGui::End();
        ImGui::Render();
        ImGui_ImplGX2_RenderDrawData(ImGui::GetDrawData());
    }

    state.interpreter->EndFrame();

    if (!resultsPath.empty() && periodicResultsCounter-- <= 0) {
        FILE* f = std::fopen(resultsPath.c_str(), "w");
        if (f != nullptr) {
            std::fprintf(f, "libultraship Wii U harness - graphics: context + display list\n");
            std::fprintf(f, "frame: %u\n", state.frameCount);
            std::fprintf(f, "archive init: %s\n", state.archiveInitialized ? "OK" : "FAILED");
            std::fprintf(f, "fixture file resolved: %s\n", state.fixtureFileResolved ? "OK" : "FAILED");
            std::fprintf(f, "font probe (expect absent): %s\n",
                         state.fontProbeAbsentAsExpected ? "OK" : "UNEXPECTED");
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

    const std::string harnessDir = HarnessDirPath();
    const std::string resultsPath = harnessDir.empty() ? "" : harnessDir + "/results.txt";
    const bool sdWriteOk = !resultsPath.empty();

    char heapLineBuf[64];
    std::snprintf(heapLineBuf, sizeof(heapLineBuf), "MEM2 heap free: %u bytes", DefaultHeapFreeBytes());
    const std::string heapLine = heapLineBuf;

    const AtomicStressResult atomicStress = RunAtomicStressTest();

    if (sdWriteOk) {
        WriteBootLinkResults(resultsPath, heapLine, atomicStress);
    }

    Mode mode = Mode::Menu;
    int menuCursor = 0;
    uint32_t prevGamePadHeld = 0;
    int periodicResultsCounter = 0;
    AudioTestState audioTestState;
    ControlDeckTestState controlDeckTestState;
    Gx2TestState gx2TestState;
    ContextTestState contextTestState;
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
                if (mode == Mode::InputMapped && sdWriteOk) {
                    EnsureControlDeckTest(controlDeckTestState, harnessDir);
                } else if (mode == Mode::Audio && sdWriteOk) {
                    StartAudioTest(audioTestState, harnessDir);
                } else if (mode == Mode::Gx2Renderer) {
                    StartGx2Test(gx2TestState);
                } else if (mode == Mode::FullContext && sdWriteOk) {
                    EnsureContextTest(contextTestState, harnessDir);
                    if (!contextTestState.contextBootFailed) {
                        StartContextGraphicsTest(contextTestState);
                    }
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
        } else if (mode == Mode::FullContext && contextTestState.window != nullptr) {
            // Same one-way rationale as Gx2Renderer above - GX2 has taken the screen. Only true
            // once graphics actually came up; a failed EnsureContextTest() (no SD card, or the
            // archive/Context boot itself failed) leaves window null and falls through to the
            // generic B-returns-to-menu handling below instead, since OSScreen never left.
            if (pressed & Ship::WiiU::WIIU_BUTTON_B) {
                StopContextGraphicsTest(contextTestState);
                quitRequested = true;
                continue; // skip PumpAndRenderContextTest below - ImGui was just torn down
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
        if (mode == Mode::FullContext && contextTestState.window != nullptr) {
            // Same reasoning as Gx2Renderer above - only once graphics actually came up
            // (EnsureContextTest() may have failed and left window null, in which case the
            // Mode::FullContext switch case below shows the OSScreen failure message instead).
            PumpAndRenderContextTest(contextTestState, resultsPath, periodicResultsCounter);
            continue;
        }

        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        switch (mode) {
            case Mode::Menu:
                RenderMenu(menuCursor);
                break;
            case Mode::BootLink:
                RenderBootLink(resultsPath, heapLine, sdWriteOk, atomicStress);
                break;
            case Mode::InputReadout:
                RenderInputReadout();
                if (sdWriteOk && periodicResultsCounter-- <= 0) {
                    WriteInputResults(resultsPath);
                    periodicResultsCounter = 30; // roughly once a second at 33ms/frame
                }
                break;
            case Mode::InputMapped:
                PumpControlDeckTest(controlDeckTestState);
                RenderControlDeckTest(controlDeckTestState);
                if (sdWriteOk && periodicResultsCounter-- <= 0) {
                    WriteControlDeckResults(resultsPath, controlDeckTestState);
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
            case Mode::FullContext:
                RenderContextTestStatus(contextTestState);
                break;
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
