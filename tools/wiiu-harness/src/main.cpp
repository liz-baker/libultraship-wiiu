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
#include <whb/proc.h>
#include <whb/sdcard.h>

#include "ship/audio/AudioPlayer.h"
#include "ship/port/wiiu/WiiUImpl.h"
#include "ship/port/wiiu/WiiUInput.h"

namespace {

void* sScreenBufferTV = nullptr;
void* sScreenBufferDRC = nullptr;

enum class Mode { Menu, BootLink, InputReadout, Audio };

struct MenuItem {
    const char* label;
    Mode mode;
};

constexpr MenuItem kMenuItems[] = {
    { "Stage 0: Boot & Link", Mode::BootLink },
    { "Stage 1: Input Readout", Mode::InputReadout },
    { "Stage 2: AX Audio", Mode::Audio },
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
    std::fprintf(f, "libultraship Wii U harness - Stage 0: boot & link\n");
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
    std::fprintf(f, "stage 0: %s\n", atomicStress.Passed() ? "PASS" : "FAIL");
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

void RenderBootLink(const std::string& resultsPath, const std::string& heapLine, bool sdWriteOk,
                    const AtomicStressResult& atomicStress) {
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

    const AtomicStressResult atomicStress = RunAtomicStressTest();

    if (sdWriteOk) {
        WriteBootLinkResults(resultsPath, heapLine, atomicStress);
    }

    Mode mode = Mode::Menu;
    int menuCursor = 0;
    uint32_t prevGamePadHeld = 0;
    int periodicResultsCounter = 0;
    AudioTestState audioTestState;

    while (WHBProcIsRunning()) {
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
                }
            }
        } else if (pressed & Ship::WiiU::WIIU_BUTTON_B) {
            if (mode == Mode::Audio) {
                StopAudioTest(audioTestState);
            }
            mode = Mode::Menu;
        } else if (mode == Mode::Audio && (pressed & Ship::WiiU::WIIU_BUTTON_X)) {
            audioTestState.channelMode = NextChannelMode(audioTestState.channelMode);
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
            case Mode::Audio:
                PumpAudioTest(audioTestState);
                RenderAudioTest(audioTestState);
                if (sdWriteOk && periodicResultsCounter-- <= 0) {
                    WriteAudioResults(resultsPath, audioTestState);
                    periodicResultsCounter = 30; // roughly once a second at 33ms/frame
                }
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
