#include <cstdint>
#include <cstdio>
#include <cstring>
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

#include "ship/port/wiiu/WiiUImpl.h"
#include "ship/port/wiiu/WiiUInput.h"

namespace {

void* sScreenBufferTV = nullptr;
void* sScreenBufferDRC = nullptr;

enum class Mode { BootLink, InputReadout };

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
    PrintBoth(row++, "Press + to switch mode. Press HOME to exit.");
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
    PrintBoth(row++, "Press + to switch mode. Press HOME to exit.");
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

    Mode mode = Mode::BootLink;
    uint32_t prevGamePadHeld = 0;
    int inputResultsCounter = 0;

    while (WHBProcIsRunning()) {
        Ship::WiiU::Update();

        const uint32_t gamePadHeld = Ship::WiiU::GetButtonsHeld(Ship::WiiU::WIIU_DEVICE_GAMEPAD);
        const bool plusPressed =
            (gamePadHeld & Ship::WiiU::WIIU_BUTTON_PLUS) && !(prevGamePadHeld & Ship::WiiU::WIIU_BUTTON_PLUS);
        prevGamePadHeld = gamePadHeld;

        if (plusPressed) {
            mode = mode == Mode::BootLink ? Mode::InputReadout : Mode::BootLink;
            inputResultsCounter = 0;
        }

        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        switch (mode) {
            case Mode::BootLink:
                RenderBootLink(resultsPath, heapLine, sdWriteOk);
                break;
            case Mode::InputReadout:
                RenderInputReadout();
                if (sdWriteOk && inputResultsCounter-- <= 0) {
                    WriteInputResults(resultsPath);
                    inputResultsCounter = 30; // roughly once a second at 33ms/frame
                }
                break;
        }

        DCFlushRange(sScreenBufferTV, bufferSizeTV);
        DCFlushRange(sScreenBufferDRC, bufferSizeDRC);
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);

        OSSleepTicks(OSMillisecondsToTicks(33));
    }

    Ship::WiiU::Exit();

    MEMFreeToDefaultHeap(sScreenBufferTV);
    MEMFreeToDefaultHeap(sScreenBufferDRC);
    WHBUnmountSdCard();
    WHBProcShutdown();
    return 0;
}
