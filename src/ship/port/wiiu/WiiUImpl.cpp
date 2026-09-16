#ifdef __WIIU__
#include "WiiUImpl.h"

#include <cerrno>
#include <cstring>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/iosupport.h>

#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/sdcard.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>

namespace Ship {
namespace WiiU {

static bool hasVpad = false;
static VPADReadError vpadError = VPAD_READ_SUCCESS;
static VPADStatus vpadStatus;

static bool hasKpad[4] = { false };
static KPADError kpadError[4] = { KPAD_ERROR_OK };
static KPADStatus kpadStatus[4];

static bool sSdCardMounted = false;

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

// Mounts the SD card and makes sd:/wiiu/apps/<shortName>/ the process's working directory.
//
// This used to be raw mkdir()/chdir() straight against the hardcoded path "/vol/external01/..."
// (the WUT path alias for the SD card), with no mount call of its own and every return value
// discarded. That's the same directory WHBMountSdCard()/WHBGetSdCardMountPath() resolve to, but
// getting there without going through WHBMountSdCard() first meant this ran before anything had
// confirmed the console's SD/FS subsystem was actually ready - the same startup race documented
// for liz-baker/lus-wiiu-harness#21 (an unretried WHBMountSdCard() call failing on hardware that
// had a working SD card the whole time). A failure here was invisible: mkdir() failing left the
// next mkdir() cascading-failing against a nonexistent parent, chdir() then failing silently and
// leaving the working directory wherever the loader started the process - see
// liz-baker/lus-wiiu-harness#4. Retrying the mount call a few times, checking every return value,
// and logging failures (mirroring the harness's own HarnessDirPath()) makes a real failure visible
// instead of silently leaving the process off the SD card.
static void MountSdCardAndChdir(const std::string& shortName) {
    constexpr int kMountAttempts = 5;
    constexpr OSTime kMountRetryDelayMs = 200;

    for (int attempt = 1; attempt <= kMountAttempts; attempt++) {
        if (WHBMountSdCard()) {
            sSdCardMounted = true;
            break;
        }
        WHBLogPrintf("Ship::WiiU::Init: WHBMountSdCard() failed (attempt %d/%d)", attempt, kMountAttempts);
        if (attempt < kMountAttempts) {
            OSSleepTicks(OSMillisecondsToTicks(kMountRetryDelayMs));
        }
    }
    if (!sSdCardMounted) {
        WHBLogPrint("Ship::WiiU::Init: giving up on SD card mount after retries; working directory left unchanged");
        return;
    }

    const std::string wiiuDir = std::string(WHBGetSdCardMountPath()) + "wiiu/";
    const std::string appsDir = wiiuDir + "apps/";
    const std::string appDir = appsDir + shortName + "/";
    for (const std::string& dir : { wiiuDir, appsDir, appDir }) {
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            WHBLogPrintf("Ship::WiiU::Init: mkdir(%s) failed: %s", dir.c_str(), strerror(errno));
        }
    }

    if (chdir(appDir.c_str()) != 0) {
        WHBLogPrintf("Ship::WiiU::Init: chdir(%s) failed: %s", appDir.c_str(), strerror(errno));
    }
}

void Init(const std::string& shortName) {
#ifdef _DEBUG
    WHBLogUdpInit();
    WHBLogPrint("Hello World!");

    devoptab_list[STD_OUT] = &dotab_stdout;
    devoptab_list[STD_ERR] = &dotab_stdout;
#endif

    MountSdCardAndChdir(shortName);

    // Bring up native input. SDL3 is unavailable on the Wii U, so we read the
    // VPAD (gamepad) and KPAD (Wii Remote / Pro Controller) devices directly.
    VPADInit();
    KPADInit();
    WPADEnableURCC(TRUE);
}

void Exit() {
    KPADShutdown();

    if (sSdCardMounted) {
        WHBUnmountSdCard();
        sSdCardMounted = false;
    }

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
    VPADReadError err;
    int32_t read = VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &err);
    vpadError = err;
    hasVpad = (read > 0 && err == VPAD_READ_SUCCESS);

    // Wii Remotes / Pro Controllers on the four KPAD channels
    for (int chan = 0; chan < 4; chan++) {
        KPADError kerr = KPAD_ERROR_OK;
        int32_t kread = KPADReadEx((KPADChan)chan, &kpadStatus[chan], 1, &kerr);
        kpadError[chan] = kerr;
        hasKpad[chan] = (kread > 0 && kerr == KPAD_ERROR_OK);
    }

    UpdateRumble();
}

VPADStatus* GetVPADStatus(VPADReadError* error) {
    *error = vpadError;
    return hasVpad ? &vpadStatus : nullptr;
}

KPADStatus* GetKPADStatus(WPADChan chan, KPADError* error) {
    *error = kpadError[chan];
    return hasKpad[chan] ? &kpadStatus[chan] : nullptr;
}

}; // namespace WiiU
}; // namespace Ship

#endif
