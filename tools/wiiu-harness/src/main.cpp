#include <cstdio>
#include <cstring>
#include <string>
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

namespace {

void* sScreenBufferTV = nullptr;
void* sScreenBufferDRC = nullptr;

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

    const std::string resultsPath = ResultsFilePath();
    const bool sdWriteOk = !resultsPath.empty();

    char heapLine[64];
    std::snprintf(heapLine, sizeof(heapLine), "MEM2 heap free: %u bytes", DefaultHeapFreeBytes());

    if (sdWriteOk) {
        FILE* f = std::fopen(resultsPath.c_str(), "w");
        if (f != nullptr) {
            std::fprintf(f, "libultraship Wii U harness - Stage 0: boot & link\n");
            std::fprintf(f, "compiler: %s\n", __VERSION__);
            std::fprintf(f, "built: %s %s\n", __DATE__, __TIME__);
            std::fprintf(f, "%s\n", heapLine);
            std::fprintf(f, "sd write: PASS (%s)\n", resultsPath.c_str());
            std::fprintf(f, "stage 0: PASS\n");
            std::fclose(f);
        }
    }

    while (WHBProcIsRunning()) {
        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

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
        PrintBoth(row++, "Press HOME to exit.");

        DCFlushRange(sScreenBufferTV, bufferSizeTV);
        DCFlushRange(sScreenBufferDRC, bufferSizeDRC);
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);

        OSSleepTicks(OSMillisecondsToTicks(33));
    }

    MEMFreeToDefaultHeap(sScreenBufferTV);
    MEMFreeToDefaultHeap(sScreenBufferDRC);
    WHBUnmountSdCard();
    WHBProcShutdown();
    return 0;
}
