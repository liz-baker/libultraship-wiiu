#ifdef FAST3D_PROFILE

#include "fast/profiler.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#ifdef __WIIU__
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
#include <whb/log.h>
#endif

namespace Fast::Prof {

Experiments gExp;

namespace {

constexpr const char* kStageNames[STAGE_COUNT] = {
#define X(name) #name,
    FAST3D_PROF_STAGES(X)
#undef X
};

constexpr const char* kCounterNames[COUNTER_COUNT] = {
#define X(name) #name,
    FAST3D_PROF_COUNTERS(X)
#undef X
};

// Ticks are the platform's cheapest monotonic counter, widened to 64 bits; the Wii U's is 32 bits.
#ifdef __WIIU__
uint64_t Now() {
    static uint32_t prev = OSGetSystemTick();
    static uint64_t total = 0;
    const uint32_t t = OSGetSystemTick();
    total += static_cast<uint32_t>(t - prev);
    prev = t;
    return total;
}
double TicksPerMs() {
    return OSTimerClockSpeed / 1000.0;
}
#else
uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
double TicksPerMs() {
    return 1e6;
}
#endif

struct State {
    static constexpr int kMaxDepth = 24;
    Stage stack[kMaxDepth] = { STAGE_OTHER };
    int depth = 0;
    uint64_t lastT = 0;
    uint64_t stageTicks[STAGE_COUNT] = {};
    uint64_t counters[COUNTER_COUNT] = {};
    const char* userNames[4] = { "USER0", "USER1", "USER2", "USER3" };
    std::thread::id owner;
    bool started = false;

    uint64_t frames = 0;
    uint64_t windowStart = 0;
    uint64_t lastFrameEnd = 0;
    uint64_t frameMaxTicks = 0;
    uint64_t frameSumTicks = 0;
    uint64_t buckets[4] = {};
    bool printedExperiments = false;

    // --exp cycle=N: every N reported seconds the next entry of kCycle replaces the experiments given
    // on the command line, which come back for the first entry.
    int cycleSeconds = 0;
    int cycleWindow = 0;
    int cycleStep = 0;
    Experiments base;
};
State s;

struct CycleStep {
    const char* label;
    void (*apply)(Experiments&);
};

// Each step changes one thing so the difference from "baseline" is that thing's cost. The steps
// that remove work (skip_*, tri_limit) change what is on screen.
const CycleStep kCycle[] = {
    { "baseline", [](Experiments&) {} },
    { "skip_draw", [](Experiments& e) { e.skip_draw = 1; } },
    { "skip_tris", [](Experiments& e) { e.skip_tris = 1; } },
    { "tri_limit=500", [](Experiments& e) { e.tri_limit = 500; } },
    { "no_blend", [](Experiments& e) { e.no_blend = 1; } },
    { "no_depth", [](Experiments& e) { e.no_depth = 1; } },
    { "no_fog", [](Experiments& e) { e.no_fog = 1; } },
    { "point_filter", [](Experiments& e) { e.point_filter = 1; } },
    { "measure_gpu", [](Experiments& e) { e.measure_gpu = 1; } },
    { "swap_interval=1", [](Experiments& e) { e.swap_interval = 1; } },
};
constexpr int kCycleExperiments = sizeof(kCycle) / sizeof(kCycle[0]) - 1;
// Every other step is the baseline, so each experiment has a reference taken right beside it.
const CycleStep& StepFor(int step) {
    return step % 2 == 0 ? kCycle[0] : kCycle[1 + (step / 2) % kCycleExperiments];
}
constexpr int kCycleSteps = 2 * kCycleExperiments;

bool Mine() {
    return s.started && std::this_thread::get_id() == s.owner;
}

void Emit(const std::string& line) {
    spdlog::info("{}", line);
#ifdef __WIIU__
    WHBLogPrint(line.c_str());
#endif
}

void Start() {
    s.started = true;
    s.owner = std::this_thread::get_id();
    s.lastT = Now();
    s.windowStart = s.lastT;
    s.lastFrameEnd = s.lastT;
}

void PrintExperiments() {
    std::string line = "[prof] experiments:";
    char buf[64];
#define SHOW(label, field, fmt)                                    \
    if (gExp.field) {                                              \
        snprintf(buf, sizeof(buf), " " label "=" fmt, gExp.field); \
        line += buf;                                               \
    }
    SHOW("skip_tris", skip_tris, "%d")
    SHOW("tri_limit", tri_limit, "%d")
    SHOW("skip_draw", skip_draw, "%d")
    SHOW("point_filter", point_filter, "%d")
    SHOW("no_blend", no_blend, "%d")
    SHOW("no_depth", no_depth, "%d")
    SHOW("no_fog", no_fog, "%d")
    SHOW("measure_gpu", measure_gpu, "%d")
    SHOW("swap_interval", swap_interval, "%d")
    SHOW("res", resolution, "%.2f")
#undef SHOW
    Emit(line == "[prof] experiments:" ? "[prof] experiments: none" : line);
}

void Report(uint64_t now) {
    const double perMs = TicksPerMs();
    const double seconds = (now - s.windowStart) / (perMs * 1000.0);
    const double frames = static_cast<double>(s.frames);
    char buf[256];

    snprintf(buf, sizeof(buf),
             "[prof] %.1fs frames=%llu fps=%.1f frame ms avg=%.1f max=%.1f  <=17:%llu <=34:%llu <=51:%llu more:%llu "
             "dropped=%llu",
             seconds, (unsigned long long)s.frames, frames / seconds, s.frameSumTicks / frames / perMs,
             s.frameMaxTicks / perMs, (unsigned long long)s.buckets[0], (unsigned long long)s.buckets[1],
             (unsigned long long)s.buckets[2], (unsigned long long)s.buckets[3],
             (unsigned long long)s.counters[COUNTER_DROPPED_FRAMES]);
    Emit(buf);

    // ms per frame, in two lines so a UDP log line stays short.
    std::string line = "[prof] ms/frame:";
    int onLine = 0;
    for (int i = 0; i < STAGE_COUNT; i++) {
        if (s.stageTicks[i] == 0) {
            continue;
        }
        const char* name = i >= STAGE_USER0 && i <= STAGE_USER3 ? s.userNames[i - STAGE_USER0] : kStageNames[i];
        snprintf(buf, sizeof(buf), " %s=%.2f", name, s.stageTicks[i] / frames / perMs);
        line += buf;
        if (++onLine == 8) {
            Emit(line);
            line = "[prof] ms/frame:";
            onLine = 0;
        }
    }
    if (onLine) {
        Emit(line);
    }

    line = "[prof] /frame:";
    onLine = 0;
    for (int i = 0; i < COUNTER_COUNT; i++) {
        if (s.counters[i] == 0 || i == COUNTER_DROPPED_FRAMES) {
            continue;
        }
        if (i == COUNTER_UPLOAD_BYTES) {
            snprintf(buf, sizeof(buf), " UPLOAD_KB=%.1f", s.counters[i] / frames / 1024.0);
        } else {
            snprintf(buf, sizeof(buf), " %s=%.1f", kCounterNames[i], s.counters[i] / frames);
        }
        line += buf;
        if (++onLine == 6) {
            Emit(line);
            line = "[prof] /frame:";
            onLine = 0;
        }
    }
    if (onLine) {
        Emit(line);
    }

    memset(s.stageTicks, 0, sizeof(s.stageTicks));
    memset(s.counters, 0, sizeof(s.counters));
    memset(s.buckets, 0, sizeof(s.buckets));
    s.frames = 0;
    s.frameMaxTicks = 0;
    s.frameSumTicks = 0;
    s.windowStart = now;
}

} // namespace

bool SetExperiment(const char* name, double value) {
    struct Entry {
        const char* name;
        int* i;
        float* f;
    };
    const Entry entries[] = {
        { "skip_tris", &gExp.skip_tris, nullptr },
        { "tri_limit", &gExp.tri_limit, nullptr },
        { "skip_draw", &gExp.skip_draw, nullptr },
        { "point_filter", &gExp.point_filter, nullptr },
        { "no_blend", &gExp.no_blend, nullptr },
        { "no_depth", &gExp.no_depth, nullptr },
        { "no_fog", &gExp.no_fog, nullptr },
        { "measure_gpu", &gExp.measure_gpu, nullptr },
        { "swap_interval", &gExp.swap_interval, nullptr },
        { "res", nullptr, &gExp.resolution },
    };
    if (strcmp(name, "cycle") == 0) {
        s.cycleSeconds = static_cast<int>(value);
        return true;
    }
    for (const Entry& e : entries) {
        if (strcmp(name, e.name) == 0) {
            if (e.i) {
                *e.i = static_cast<int>(value);
            } else {
                *e.f = static_cast<float>(value);
            }
            return true;
        }
    }
    return false;
}

bool SetExperimentFromArg(const char* nameEqualsValue) {
    const char* eq = strchr(nameEqualsValue, '=');
    const std::string name = eq ? std::string(nameEqualsValue, eq - nameEqualsValue) : nameEqualsValue;
    char* end = nullptr;
    const double value = eq ? strtod(eq + 1, &end) : 1.0;
    if (eq && (end == eq + 1 || *end != '\0')) {
        return false;
    }
    return SetExperiment(name.c_str(), value);
}

void Enter(Stage stage) {
    if (!Mine()) {
        return;
    }
    const uint64_t t = Now();
    s.stageTicks[s.stack[s.depth]] += t - s.lastT;
    s.lastT = t;
    if (s.depth + 1 < State::kMaxDepth) {
        s.stack[++s.depth] = stage;
    }
}

void Exit() {
    if (!Mine() || s.depth == 0) {
        return;
    }
    const uint64_t t = Now();
    s.stageTicks[s.stack[s.depth]] += t - s.lastT;
    s.lastT = t;
    s.depth--;
}

void Switch(Stage stage) {
    if (!Mine()) {
        return;
    }
    const uint64_t t = Now();
    s.stageTicks[s.stack[s.depth]] += t - s.lastT;
    s.lastT = t;
    s.stack[s.depth] = stage;
}

void Count(Counter counter, uint64_t n) {
    if (Mine()) {
        s.counters[counter] += n;
    }
}

void SetUserStageName(Stage userStage, const char* name) {
    if (userStage >= STAGE_USER0 && userStage <= STAGE_USER3) {
        s.userNames[userStage - STAGE_USER0] = name;
    }
}

void FrameBegin() {
    if (!s.started) {
        Start();
        s.base = gExp;
        PrintExperiments();
        if (s.cycleSeconds > 0) {
            Emit("[prof] cycle step 0: baseline");
            Emit("[prof] cycle: " + std::to_string(s.cycleSeconds) + "s per step, " + std::to_string(kCycleSteps) +
                 " steps, repeating");
        }
    }
}

void FrameEnd() {
    if (!Mine()) {
        return;
    }
    const uint64_t t = Now();
    s.stageTicks[s.stack[s.depth]] += t - s.lastT;
    s.lastT = t;

    const uint64_t frame = t - s.lastFrameEnd;
    s.lastFrameEnd = t;
    const double ms = frame / TicksPerMs();
    s.frames++;
    s.frameSumTicks += frame;
    if (frame > s.frameMaxTicks) {
        s.frameMaxTicks = frame;
    }
    s.buckets[ms <= 17.5 ? 0 : ms <= 34.5 ? 1 : ms <= 51.5 ? 2 : 3]++;

    if (t - s.windowStart >= static_cast<uint64_t>(TicksPerMs() * 1000.0)) {
        Report(t);
        if (s.cycleSeconds > 0 && ++s.cycleWindow >= s.cycleSeconds) {
            s.cycleWindow = 0;
            s.cycleStep = (s.cycleStep + 1) % kCycleSteps;
            gExp = s.base;
            StepFor(s.cycleStep).apply(gExp);
            Emit(std::string("[prof] cycle step ") + std::to_string(s.cycleStep) + ": " + StepFor(s.cycleStep).label);
        }
    }
}

} // namespace Fast::Prof

#endif // FAST3D_PROFILE
