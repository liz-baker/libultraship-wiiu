#ifdef __WIIU__
#include "WiiUImpl.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/iosupport.h>

#include <whb/log.h>
#include <whb/log_udp.h>
#include <coreinit/debug.h>

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
#ifdef _DEBUG
    WHBLogUdpInit();
    WHBLogPrint("Hello World!");

    devoptab_list[STD_OUT] = &dotab_stdout;
    devoptab_list[STD_ERR] = &dotab_stdout;
#endif

    // make sure the required folders exist
    mkdir("/vol/external01/wiiu/", 0755);
    mkdir("/vol/external01/wiiu/apps/", 0755);
    mkdir(("/vol/external01/wiiu/apps/" + shortName + "/").c_str(), 0755);

    chdir(("/vol/external01/wiiu/apps/" + shortName + "/").c_str());

    // Bring up native input. SDL3 is unavailable on the Wii U, so we read the
    // VPAD (gamepad) and KPAD (Wii Remote / Pro Controller) devices directly.
    VPADInit();
    KPADInit();
    WPADEnableURCC(TRUE);
}

void Exit() {
    KPADShutdown();

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
