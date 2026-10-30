#ifdef FAST3D_PROFILE

#include "fast/profiler.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/interpreter.h"

#ifdef __WIIU__
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
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
    const char* userNames[12] = { "USER0", "USER1", "USER2", "USER3", "USER4",  "USER5",
                                  "USER6", "USER7", "USER8", "USER9", "USER10", "USER11" };
    const char* userCounterNames[4] = { "USER_A", "USER_B", "USER_C", "USER_D" };
    const char* userExperimentNames[4] = {};
    // Names `--exp` gave that nothing has claimed yet: the game registers its own after the arguments
    // are parsed.
    std::vector<std::pair<std::string, double>> pendingExperiments;
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

    int curOpcode = -1;
    uint64_t opTicks[256] = {};
    uint64_t opCount[256] = {};
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
constexpr int kFixedExperiments = sizeof(kCycle) / sizeof(kCycle[0]) - 1;

// The experiments the game named come after the fixed ones.
int NamedUserExperiment(int nth) {
    for (int i = 0; i < 4; i++) {
        if (s.userExperimentNames[i] && nth-- == 0) {
            return i;
        }
    }
    return -1;
}
int CycleExperiments() {
    int n = kFixedExperiments;
    for (int i = 0; i < 4; i++) {
        n += s.userExperimentNames[i] != nullptr;
    }
    return n;
}
// Every other step is the baseline, so each experiment has a reference taken right beside it.
int CycleSteps() {
    return 2 * CycleExperiments();
}
const char* StepLabel(int step) {
    if (step % 2 == 0) {
        return kCycle[0].label;
    }
    const int k = (step / 2) % CycleExperiments();
    return k < kFixedExperiments ? kCycle[1 + k].label
                                 : s.userExperimentNames[NamedUserExperiment(k - kFixedExperiments)];
}
void ApplyStep(int step, Experiments& e) {
    if (step % 2 == 0) {
        return;
    }
    const int k = (step / 2) % CycleExperiments();
    if (k < kFixedExperiments) {
        kCycle[1 + k].apply(e);
        return;
    }
    int* const users[4] = { &e.user0, &e.user1, &e.user2, &e.user3 };
    *users[NamedUserExperiment(k - kFixedExperiments)] = 1;
}

void Account(uint64_t t) {
    const uint64_t dt = t - s.lastT;
    s.stageTicks[s.stack[s.depth]] += dt;
    if (s.curOpcode >= 0 && s.stack[s.depth] == STAGE_RUN) {
        s.opTicks[s.curOpcode] += dt;
    }
}

bool Mine() {
    return s.started && std::this_thread::get_id() == s.owner;
}

// The logger reaches the UDP log on the Wii U too (stdout is redirected there), so this is the only
// output; writing each line twice overran the UDP socket.
void Emit(const std::string& line) {
    spdlog::info("{}", line);
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
    SHOW("user0", user0, "%d")
    SHOW("user1", user1, "%d")
    SHOW("user2", user2, "%d")
    SHOW("user3", user3, "%d")
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
        const char* name = i >= STAGE_USER0 && i <= STAGE_USER11 ? s.userNames[i - STAGE_USER0] : kStageNames[i];
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
            const char* name =
                i >= COUNTER_USER_A && i <= COUNTER_USER_D ? s.userCounterNames[i - COUNTER_USER_A] : kCounterNames[i];
            snprintf(buf, sizeof(buf), " %s=%.1f", name, s.counters[i] / frames);
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

    // The commands the RUN stage spends most of its own time on.
    int top[8];
    int nTop = 0;
    for (int op = 0; op < 256; op++) {
        if (s.opTicks[op] == 0) {
            continue;
        }
        int at = nTop < 8 ? nTop++ : 8;
        top[at < 8 ? at : 7] = at < 8 ? op : (s.opTicks[op] > s.opTicks[top[7]] ? op : top[7]);
        for (int j = (at < 8 ? at : 7); j > 0 && s.opTicks[top[j]] > s.opTicks[top[j - 1]]; j--) {
            std::swap(top[j], top[j - 1]);
        }
    }
    if (nTop) {
        line = "[prof] RUN by command (ms/frame, calls/frame):";
        for (int i = 0; i < nTop; i++) {
            snprintf(buf, sizeof(buf), " %s=%.2f,%.0f", GfxGetOpcodeName(static_cast<int8_t>(top[i])),
                     s.opTicks[top[i]] / frames / perMs, s.opCount[top[i]] / frames);
            line += buf;
            if (i == 3) {
                Emit(line);
                line = "[prof] RUN by command:";
            }
        }
        Emit(line);
    }
    memset(s.opTicks, 0, sizeof(s.opTicks));
    memset(s.opCount, 0, sizeof(s.opCount));
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
    int* const users[4] = { &gExp.user0, &gExp.user1, &gExp.user2, &gExp.user3 };
    for (int i = 0; i < 4; i++) {
        const std::string generic = "user" + std::to_string(i);
        if (generic == name || (s.userExperimentNames[i] && strcmp(s.userExperimentNames[i], name) == 0)) {
            *users[i] = static_cast<int>(value);
            return true;
        }
    }
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
    s.pendingExperiments.emplace_back(name, value);
    return true;
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
    Account(t);
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
    Account(t);
    s.lastT = t;
    s.depth--;
}

void Switch(Stage stage) {
    if (!Mine()) {
        return;
    }
    const uint64_t t = Now();
    Account(t);
    s.lastT = t;
    s.stack[s.depth] = stage;
}

void Count(Counter counter, uint64_t n) {
    if (Mine()) {
        s.counters[counter] += n;
    }
}

void OpcodeBegin(int8_t opcode) {
    if (!Mine()) {
        return;
    }
    s.curOpcode = static_cast<uint8_t>(opcode);
    s.opCount[s.curOpcode]++;
    s.counters[COUNTER_COMMANDS]++;
}

void OpcodeEnd() {
    s.curOpcode = -1;
}

void SetUserCounterName(Counter userCounter, const char* name) {
    if (userCounter >= COUNTER_USER_A && userCounter <= COUNTER_USER_D) {
        s.userCounterNames[userCounter - COUNTER_USER_A] = name;
    }
}

void SetUserExperimentName(int index, const char* name) {
    if (index < 0 || index >= 4) {
        return;
    }
    s.userExperimentNames[index] = name;
    for (size_t i = 0; i < s.pendingExperiments.size();) {
        if (s.pendingExperiments[i].first == name) {
            const double value = s.pendingExperiments[i].second;
            s.pendingExperiments.erase(s.pendingExperiments.begin() + i);
            SetExperiment(name, value);
        } else {
            i++;
        }
    }
}

void SetUserStageName(Stage userStage, const char* name) {
    if (userStage >= STAGE_USER0 && userStage <= STAGE_USER11) {
        s.userNames[userStage - STAGE_USER0] = name;
    }
}

void FrameBegin() {
    if (!s.started) {
        Start();
        for (const auto& pending : s.pendingExperiments) {
            Emit("[prof] unknown experiment ignored: " + pending.first);
        }
        s.base = gExp;
        PrintExperiments();
        if (s.cycleSeconds > 0) {
            Emit("[prof] cycle step 0: baseline");
            Emit("[prof] cycle: " + std::to_string(s.cycleSeconds) + "s per step, " + std::to_string(CycleSteps()) +
                 " steps, repeating");
        }
    }
}

void FrameEnd() {
    if (!Mine()) {
        return;
    }
    const uint64_t t = Now();
    Account(t);
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
            s.cycleStep = (s.cycleStep + 1) % CycleSteps();
            gExp = s.base;
            ApplyStep(s.cycleStep, gExp);
            Emit(std::string("[prof] cycle step ") + std::to_string(s.cycleStep) + ": " + StepLabel(s.cycleStep));
        }
    }
}

} // namespace Fast::Prof

#endif // FAST3D_PROFILE
