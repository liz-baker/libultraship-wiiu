#ifdef __WIIU__
#include "WiiUImpl.h"

#include <stdio.h>
#include <sys/iosupport.h>

#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/sdcard.h>
#include <coreinit/debug.h>

#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>

#include <spdlog/spdlog.h>

namespace Ship {
namespace WiiU {

static bool hasVpad = false;
static VPADReadError vpadError = VPAD_READ_SUCCESS;
static VPADStatus vpadStatus;

static bool hasKpad[4] = { false };
static KPADError kpadError[4] = { KPAD_ERROR_OK };
static KPADStatus kpadStatus[4];

// A poll that returns no new sample is normal, and reporting the device as unplugged for it makes
// the game route its controllers again (a stall of seconds on the SD card, which makes the next
// poll miss as well). A device counts as gone only after this many polls in a row without one,
// about half a second at 60 frames a second; until then its last status stands.
static constexpr int kMissedPollsBeforeLost = 30;
static int vpadMissed = 0;
static int kpadMissed[4] = { 0 };

#ifdef _DEBUG
extern "C" {
void __wrap_abort() {
    printf("Abort called.\n");
    // force a stack trace
    *(uint32_t*)0xdeadc0de = 0xcafebabe;
    while (1)
        ;
}

static ssize_t wiiu_log_write(struct _reent* r, void* fd, const char* ptr, size_t len) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%*.*s", (int)len, (int)len, ptr);
    OSReport(buf);
    WHBLogWritef("%*.*s", (int)len, (int)len, ptr);
    return len;
}

static const devoptab_t dotab_stdout = {
    .name = "stdout_whb",
    .write_r = wiiu_log_write,
};
};
#endif

void Init(const std::string& shortName) {
    // Context::GetAppDirectoryPath() can run before the Context exists, so both
    // paths call this; only the first does the bring-up.
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

#ifdef _DEBUG
    WHBLogUdpInit();
    WHBLogPrint("Hello World!");

    devoptab_list[STD_OUT] = &dotab_stdout;
    devoptab_list[STD_ERR] = &dotab_stdout;
#endif

    // Deliberately no filesystem/SD card setup here: this used to mkdir()/chdir() into the SD
    // card directly (raw, unchecked, no mount call), standing in for a missing Wii U case in
    // Context::GetAppDirectoryPath() - the actual, cross-platform API for "give me a writable
    // per-app directory" that every other platform already implements there (Android/iOS/Apple/
    // Linux/SDL_GetPrefPath()). That's been fixed at the right layer - see WiiUAppDirectoryPath()
    // in Context.cpp - so this stays a pure platform bring-up function: logging and input only.

    // Bring up native input. SDL3 is unavailable on the Wii U, so we read the
    // VPAD (gamepad) and KPAD (Wii Remote / Pro Controller) devices directly.
    VPADInit();
    KPADInit();
    WPADEnableURCC(TRUE);
}

void Exit() {
    KPADShutdown();

    // Safe even if Context::GetAppDirectoryPath() (Context.cpp) never mounted the card this run.
    WHBUnmountSdCard();

    WHBLogUdpDeinit();
}

void ThrowMissingOTR(const char* otrPath) {
    // TODO handle this better in the future
    OSFatal("Main OTR file not found!");
}

void ThrowInvalidOTR() {
    OSFatal("Invalid OTR files! Try regenerating them!");
}

void Update() {
    // Gamepad / DRC
    VPADStatus vpadFresh;
    VPADReadError err;
    const int32_t read = VPADRead(VPAD_CHAN_0, &vpadFresh, 1, &err);
    vpadError = err;
    if (read > 0 && err == VPAD_READ_SUCCESS) {
        vpadStatus = vpadFresh;
        if (!hasVpad) {
            SPDLOG_INFO("Wii U: the GamePad is connected");
        }
        hasVpad = true;
        vpadMissed = 0;
    } else if (hasVpad && ++vpadMissed >= kMissedPollsBeforeLost) {
        SPDLOG_WARN("Wii U: the GamePad is lost: no sample for {} polls (last read {}, error {})", vpadMissed, read,
                    static_cast<int>(err));
        hasVpad = false;
    }

    // Wii Remotes / Pro Controllers on the four KPAD channels
    for (int chan = 0; chan < 4; chan++) {
        KPADStatus kpadFresh;
        KPADError kerr = KPAD_ERROR_OK;
        const int32_t kread = KPADReadEx((KPADChan)chan, &kpadFresh, 1, &kerr);
        kpadError[chan] = kerr;
        if (kread > 0 && kerr == KPAD_ERROR_OK) {
            kpadStatus[chan] = kpadFresh;
            if (!hasKpad[chan]) {
                SPDLOG_INFO("Wii U: channel {} is connected", chan);
            }
            hasKpad[chan] = true;
            kpadMissed[chan] = 0;
        } else if (hasKpad[chan] && ++kpadMissed[chan] >= kMissedPollsBeforeLost) {
            SPDLOG_WARN("Wii U: channel {} is lost: no sample for {} polls (last read {}, error {})", chan,
                        kpadMissed[chan], kread, static_cast<int>(kerr));
            hasKpad[chan] = false;
        }
    }

    UpdateRumble();
}

VPADStatus* GetVPADStatus(VPADReadError* error) {
    // Within the grace period the last good status is what is reported, as a success.
    *error = hasVpad ? VPAD_READ_SUCCESS : vpadError;
    return hasVpad ? &vpadStatus : nullptr;
}

KPADStatus* GetKPADStatus(WPADChan chan, KPADError* error) {
    *error = hasKpad[chan] ? KPAD_ERROR_OK : kpadError[chan];
    return hasKpad[chan] ? &kpadStatus[chan] : nullptr;
}

}; // namespace WiiU
}; // namespace Ship

#endif
