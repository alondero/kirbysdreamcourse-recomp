/* Game-specific second-seat opponent for the USA ROM.
 *
 * Shots are planned off-screen: inside a private rollback snapshot, the
 * guest's own aiming preview ($81:A9B8) runs once per candidate against a
 * fixed copy of work RAM, and the snapshot is restored before the frame
 * continues. The chosen shot is then played through the ordinary aim, spin
 * and power controls. There is no host physics; see docs/cpu-opponent.md. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif
#include "cpu_opponent.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef MAX_PATH
#else
#include <time.h>
#endif

#define WRAM_SIZE 0x20000u
#define MAX_TARGETS 32u
#define MAX_CANDIDATES 128u
#define MAX_PATH 1024u
/* One guest preview costs about as much host time as a frame of play, so the
 * search advances a fixed count per frame. A count, never a clock, keeps
 * every run (and native/interpreter comparisons) identical. */
#define PREVIEWS_PER_FRAME 1u
#define SETTLE_FRAMES 24u
/* Flat-ground rolling loses about this much speed per trajectory sample;
 * used to guess past the end of a guide that does not show the slowdown. */
#define DEFAULT_DECEL 0.0016
#define PI 3.14159265358979323846

typedef struct { unsigned angle, loft, spin_x, spin_y, power; } Shot;
typedef struct { double x, y, z; } Point;
typedef struct { unsigned targets; bool banks; unsigned lofts; bool spin; unsigned step; } Policy;

enum { IDLE, THINK, AIM, SETTLE, SPIN, POWER, FLIGHT };

/* Index by difficulty. Easy keeps to direct ground shots; Medium adds low
 * lofts and spin; Hard adds banks and high lofts. Handicaps do the rest. */
static const Policy policies[4] = {
    {0, false, 0, false, 0},
    {3, false, 0, false, 4},
    {4, false, 1, true, 2},
    {4, true, 2, true, 1},
};

static unsigned difficulty, turn, phase, score_wait, settle, tap, last_pad;
static bool regs_ready, image_ready, planning, refined, applied;
static CpuState regs;
static uint8_t *image, *snapshot;
static size_t snapshot_capacity;
static uint32_t rng;
static Point targets[MAX_TARGETS];
static bool cups[MAX_TARGETS];
static unsigned target_count, max_power, horizon, bounces, marker;
static Shot queue[MAX_CANDIDATES], best;
static unsigned queued, evaluated, previews, think_frames;
static double best_score;
static int spin_slip, power_slip;
static Point path[MAX_PATH];

static unsigned Word(const uint8_t *r, unsigned a) { return r[a] | (unsigned)r[a + 1] << 8; }
static void Put(uint8_t *r, unsigned a, unsigned v) { r[a] = (uint8_t)v; r[a + 1] = (uint8_t)(v >> 8); }
static double Fixed(const uint8_t *r, unsigned lo, unsigned hi)
{ return (int16_t)Word(r, hi) + Word(r, lo) / 65536.0; }
static uint32_t Random(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

const char *KdcCpuName(unsigned d)
{
    static const char *const names[] = {"HUMAN", "EASY", "MEDIUM", "HARD"};
    return names[d < 4 ? d : 0];
}
unsigned KdcCpuHandicap(unsigned d) { return d >= 1 && d <= 3 ? 3 - d : 0; }
unsigned KdcCpuDifficulty(void) { return difficulty; }
bool KdcCpuPlanning(void) { return planning; }

void KdcCpuReset(void)
{
    turn = score_wait = settle = tap = last_pad = queued = evaluated = target_count = 0;
    phase = IDLE;
    regs_ready = image_ready = planning = refined = applied = false;
    rng = 0x4b444331;
}
void KdcCpuSelect(unsigned d) { difficulty = d <= 3 ? d : 0; KdcCpuReset(); }

static bool Match(const uint8_t *r) { return difficulty && Word(r, 0x17ff) == 1; }
static bool OwnTurn(const uint8_t *r)
{ return Match(r) && Word(r, 0x1803) == 2 && !Word(r, 0xed07); }

static void Enqueue(Shot s)
{
    if (queued >= MAX_CANDIDATES) return;
    if (s.power < 1) s.power = 1;
    if (s.power > max_power) s.power = max_power;
    for (unsigned i = 0; i < queued; ++i)
        if (!memcmp(&queue[i], &s, sizeof(s))) return;
    queue[queued++] = s;
}

/* Collect the cup and every enemy or star the CPU does not already own, and
 * queue direct, banked and lofted approaches at a spread of powers. */
static void Prepare(const uint8_t *r)
{
    const Policy *p = &policies[difficulty];
    max_power = Word(r, 0x6b3c);
    if (!max_power || max_power > 63) max_power = 63;
    /* The guide handicap (3, 2 or 1 lofted bounces) also sets how far ahead
     * the CPU judges a roll: all of it, about half, or the on-screen guide. */
    bounces = Word(r, 0x6acc);
    if (bounces < 1 || bounces > 3) bounces = 3;
    horizon = bounces == 3 ? 240 : bounces == 2 ? 120 : 40;
    marker = Word(r, 0x6b5e);
    if (marker < 1 || marker > 3) marker = 1;
    target_count = 0;
    for (unsigned i = 0; i < 64 && target_count < MAX_TARGETS; i += 2) {
        unsigned entity = Word(r, 0xb4b8 + i);
        if (entity == 0xffff) break;
        if (entity != 8 && (entity < 0x40 || entity >= 0x58)) continue;
        unsigned slot = Word(r, 0xb4f8 + i);
        if (slot >= 128 || (slot & 1) || Word(r, 0x4e1c + slot) != 0x49) continue;
        if (entity != 8 && Word(r, 0x539c + slot) == 1) continue;
        cups[target_count] = entity == 8;
        targets[target_count++] = (Point){Word(r, 0xb538 + i) + 0.5,
            Word(r, 0xb578 + i) + 0.5, Fixed(r, 0xb5b8 + i, 0xb5f8 + i)};
    }
    double ox = Fixed(r, 0xd8ac, 0xd8b0), oy = Fixed(r, 0xd8b4, 0xd8b8);
    unsigned angles[8], angle_count = 0, chosen = 0;
    for (unsigned j = 0; j < target_count && j < p->targets; ++j) {
        double nearest = 1e30;
        unsigned next = 0;
        for (unsigned t = 0; t < target_count; ++t) {
            if (chosen & (1u << t)) continue;
            double dx = targets[t].x - ox, dy = targets[t].y - oy, d = dx * dx + dy * dy;
            if (d < nearest) { nearest = d; next = t; }
        }
        chosen |= 1u << next;
        int a = (int)lround(atan2(targets[next].y - oy, targets[next].x - ox) * 180 / PI);
        angles[angle_count++] = (unsigned)((a + 360) % 360);
    }
    if (!angle_count) angles[angle_count++] = Word(r, 0xd9b4) % 360;
    if (p->banks) {
        angles[angle_count++] = (angles[0] + 45) % 360;
        angles[angle_count++] = (angles[0] + 315) % 360;
    }
    queued = evaluated = 0;
    best_score = -1e30;
    refined = false;
    for (unsigned a = 0; a < angle_count; ++a)
        for (unsigned step = 1; step <= 5; ++step)
            for (unsigned loft = 0; loft <= p->lofts; ++loft)
                Enqueue((Shot){angles[a], loft * 30, 6, 6, (max_power * step + 4) / 5});
    best = queue[0];
}

/* Read the preview path. Past the CPU's look-ahead, continue it in a straight
 * line, slowing as the guide itself shows (or at the usual rolling rate). The
 * guess cannot see walls or slopes: that is the cost of a shorter guide. */
static unsigned ReadPath(const uint8_t *r)
{
    unsigned count = Word(r, 0x6aee);
    if (!count || count > 512) return 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned o = i * 2;
        path[i] = (Point){Fixed(r, 0xb746 + o, 0xbb46 + o), Fixed(r, 0xbf46 + o, 0xc346 + o),
                          Fixed(r, 0xc746 + o, 0xcb46 + o)};
    }
    if (count < 3) return count;
    Point a = path[count - 2], b = path[count - 1];
    double vx = b.x - a.x, vy = b.y - a.y, v = sqrt(vx * vx + vy * vy);
    if (v < 1e-4) return count;
    double v0 = hypot(path[1].x - path[0].x, path[1].y - path[0].y);
    double decel = (v0 - v) / (count - 2);
    if (decel < DEFAULT_DECEL / 4) decel = DEFAULT_DECEL;
    double ux = vx / v, uy = vy / v;
    while (count < MAX_PATH && (v -= decel) > 0) {
        b.x += ux * v;
        b.y += uy * v;
        path[count++] = b;
    }
    return count;
}

static double Score(const uint8_t *r, const Shot *s)
{
    unsigned count = ReadPath(r);
    if (!count || !target_count) return -1e20;
    double score = 0, nearest = 1e20;
    Point end = path[count - 1];
    if (end.x < 0 || end.y < 0 || end.x >= Word(r, 0x8b92) || end.y >= Word(r, 0x8b94))
        score -= 500;
    double angle = s->angle * PI / 180;
    for (unsigned t = 0; t < target_count; ++t) {
        double distance = 1e20, previous_forward = 0;
        for (unsigned i = 0; i < count; ++i) {
            Point q = path[i];
            double dx = q.x - targets[t].x, dy = q.y - targets[t].y, dz = q.z - targets[t].z;
            double d = dx * dx + dy * dy + dz * dz;
            if (cups[t] && i) {
                double vx = q.x - path[i - 1].x, vy = q.y - path[i - 1].y;
                d += 4 * (vx * vx + vy * vy); /* A soft entry beats a fast pass. */
            }
            if (difficulty == KDC_CPU_EASY) {
                /* Easy only trusts the initial straight, forward corridor. */
                double fx = q.x - path[0].x, fy = q.y - path[0].y;
                double forward = fx * cos(angle) + fy * sin(angle);
                if (fabs(fx * sin(angle) - fy * cos(angle)) > 0.35 ||
                    forward + 0.01 < previous_forward) break;
                previous_forward = forward;
            }
            if (d < distance) distance = d;
        }
        if (distance < nearest) nearest = distance;
        if (distance < 0.20) score += 100;
    }
    /* Prefer closer approaches and modest power when predicted hits tie. */
    return score - sqrt(nearest) * 10 - s->power * 0.01;
}

static int EnvFlag(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] == '1' && v[1] == '\0';
}

static double NowMonoMs(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static double NowThreadCpuMs(void)
{
#ifdef _WIN32
    FILETIME ct, et, kt, ut;
    if (!GetThreadTimes(GetCurrentThread(), &ct, &et, &kt, &ut)) return 0.0;
    ULARGE_INTEGER k = {{kt.dwLowDateTime, kt.dwHighDateTime}};
    ULARGE_INTEGER u = {{ut.dwLowDateTime, ut.dwHighDateTime}};
    return (double)(k.QuadPart + u.QuadPart) / 10000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static double NowThreadCpuSample(void)
{
#ifdef _WIN32
    ULONG64 cyc = 0;
    return QueryThreadCycleTime(GetCurrentThread(), &cyc) ? (double)cyc : 0.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3; /* microseconds */
#endif
}

static int CmpDouble(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static double prof_samples[MAX_CANDIDATES * 2u], prof_cpu_samples[MAX_CANDIDATES * 2u];
static double prof_think_ms, prof_think_cpu_ms;
static unsigned prof_count, verify_checked, verify_matched, verify_mismatches;
static uint8_t *verify_new_ram, *verify_in_ram, *verify_snap;

static void VerifyPreviewAgainstOracle(const uint8_t *r_new, bool ok_new,
                                       const CpuState *c_pre,
                                       size_t snap_size)
{
    if (!RtlRollbackLoadFromMemory(verify_snap, snap_size)) {
        fputs("CPU verify: rollback restore failed\n", stderr);
        abort();
    }
    memcpy(g_cpu.ram, verify_in_ram, WRAM_SIZE);
    CpuState c_ref = *c_pre;
    c_ref.ram = g_cpu.ram;
    bool ok_ref = interp_bridge_run(&c_ref, 0x81a9b8) == 1;
    ++verify_checked;
    if (ok_new != ok_ref) {
        ++verify_mismatches;
        fprintf(stderr, "CPU verify MISMATCH candidate %u: return new=%d ref=%d\n",
                verify_checked, (int)ok_new, (int)ok_ref);
        return;
    }
    if (Word(r_new, 0x6aee) != Word(g_cpu.ram, 0x6aee)) {
        ++verify_mismatches;
        fprintf(stderr, "CPU verify MISMATCH candidate %u: $6AEE new=%u ref=%u\n",
                verify_checked, Word(r_new, 0x6aee), Word(g_cpu.ram, 0x6aee));
        return;
    }
    for (unsigned off = 0xb746u; off < 0xcf46u; ++off) {
        if (r_new[off] != g_cpu.ram[off]) {
            ++verify_mismatches;
            fprintf(stderr, "CPU verify MISMATCH candidate %u: $%04X new=%02X ref=%02X\n",
                    verify_checked, off, r_new[off], g_cpu.ram[off]);
            return;
        }
    }
    ++verify_matched;
}

/* Run one candidate through the guest preview on a fresh copy of the planning
 * image. Only called inside the private snapshot. */
static bool Preview(const Shot *s)
{
    static int prof_on = -1, verify_on = -1;
    if (prof_on < 0) prof_on = EnvFlag("KDC_CPU_PROFILE");
    if (verify_on < 0) verify_on = EnvFlag("KDC_CPU_PREVIEW_VERIFY");
    uint8_t *r = g_cpu.ram;
    memcpy(r, image, WRAM_SIZE);
    Put(r, 0xd9b4, s->angle);
    Put(r, 0xd9b6, s->loft);
    Put(r, 0xd9b8, s->spin_x);
    Put(r, 0xd9ba, s->spin_y);
    Put(r, 0x6ad6, s->loft ? 2 : 0);
    Put(r, 0x6ad8, s->loft ? 0 : 1);
    Put(r, 0x6b3c, s->power); /* The preview takes its power from here. */
    Put(r, 0x6ace, bounces);  /* A patient player's full lofted guide. */
    CpuState c = regs;
    c.ram = r;
    CpuState c_pre = c;
    size_t verify_snap_size = 0;
    if (verify_on) {
        if (!verify_in_ram) verify_in_ram = malloc(WRAM_SIZE);
        if (!verify_new_ram) verify_new_ram = malloc(WRAM_SIZE);
        if (!verify_snap) verify_snap = malloc(snapshot_capacity);
        if (!verify_in_ram || !verify_new_ram || !verify_snap) {
            fputs("CPU verify: out of memory\n", stderr);
            abort();
        }
        memcpy(verify_in_ram, r, WRAM_SIZE);
        verify_snap_size = RtlRollbackSaveToMemory(verify_snap, snapshot_capacity);
        if (!verify_snap_size) {
            fputs("CPU verify: snapshot unavailable\n", stderr);
            abort();
        }
    }
    double t0 = prof_on ? NowMonoMs() : 0.0;
    double c0 = prof_on ? NowThreadCpuSample() : 0.0;
    bool ok = KdcCpuRunPreview(&c, 0x81a9b8, horizon);
    if (prof_on && prof_count < sizeof(prof_samples) / sizeof(prof_samples[0])) {
        prof_cpu_samples[prof_count] = NowThreadCpuSample() - c0;
        prof_samples[prof_count++] = NowMonoMs() - t0;
    }
    if (verify_on) {
        memcpy(verify_new_ram, r, WRAM_SIZE);
        VerifyPreviewAgainstOracle(verify_new_ram, ok, &c_pre, verify_snap_size);
        memcpy(r, verify_new_ram, WRAM_SIZE);
    }
    return ok;
}

static void Refine(void)
{
    const Policy *p = &policies[difficulty];
    Shot centre = best;
    queued = evaluated = 0;
    for (int da = -2; da <= 2; ++da)
        for (int dp = -3; dp <= 3; dp += 3) {
            Shot s = centre;
            s.angle = (unsigned)(((int)s.angle + da * (int)p->step + 360) % 360);
            s.power = (unsigned)((int)s.power + dp < 1 ? 1 : (int)s.power + dp);
            Enqueue(s);
        }
    if (p->spin) {
        static const unsigned spins[][2] = {{3, 6}, {9, 6}, {6, 3}, {6, 9}};
        for (unsigned i = 0; i < 4; ++i) {
            if (!centre.loft && spins[i][1] != 6) continue; /* Top/backspin acts on landing. */
            Shot s = centre;
            s.spin_x = spins[i][0];
            s.spin_y = spins[i][1];
            Enqueue(s);
        }
    }
    refined = true;
}

static void Decide(void)
{
    /* One-frame reaction slips. The power meter moves a unit a frame; the
     * lofted spin marker moves a step every `marker` frames, so the faster
     * marker of a more handicapped CPU turns more slips into a wrong step. */
    int slip = (int)(Random() % 3) - 1;
    spin_slip = best.loft && Random() % marker == 0 ? slip : 0;
    power_slip = (int)(Random() % 3) - 1;
    phase = AIM;
    applied = false;
    tap = last_pad = 0;
    fprintf(stderr, "CPU %s: turn %u angle %u loft %u spin %u/%u power %u score %.2f "
            "(%u previews over %u frames)\n", KdcCpuName(difficulty), turn, best.angle, best.loft,
            best.spin_x, best.spin_y, best.power, best_score, previews, think_frames);
    if (EnvFlag("KDC_CPU_PROFILE") && prof_count > 0) {
        qsort(prof_samples, prof_count, sizeof(prof_samples[0]), CmpDouble);
        qsort(prof_cpu_samples, prof_count, sizeof(prof_cpu_samples[0]), CmpDouble);
        double total = 0.0, cpu_total_raw = 0.0;
        for (unsigned i = 0; i < prof_count; ++i) {
            total += prof_samples[i];
            cpu_total_raw += prof_cpu_samples[i];
        }
        double med = (prof_count & 1u)
            ? prof_samples[prof_count / 2u]
            : 0.5 * (prof_samples[prof_count / 2u - 1u] + prof_samples[prof_count / 2u]);
        double q1 = prof_samples[prof_count / 4u];
        double q3 = prof_samples[(prof_count * 3u) / 4u];
        double c_med = (prof_count & 1u)
            ? prof_cpu_samples[prof_count / 2u]
            : 0.5 * (prof_cpu_samples[prof_count / 2u - 1u] + prof_cpu_samples[prof_count / 2u]);
        double c_q1 = prof_cpu_samples[prof_count / 4u];
        double c_q3 = prof_cpu_samples[(prof_count * 3u) / 4u];
#ifdef _WIN32
        fprintf(stderr,
                "CPU profile: previews=%u think_ms=%.3f think_cpu_ms=%.3f "
                "preview_ms(med=%.3f min=%.3f max=%.3f iqr=%.3f total=%.3f) "
                "preview_cpu_cyc(med=%.0f min=%.0f max=%.0f iqr=%.0f total=%.0f)\n",
                prof_count, prof_think_ms, prof_think_cpu_ms,
                med, prof_samples[0], prof_samples[prof_count - 1u], q3 - q1, total,
                c_med, prof_cpu_samples[0], prof_cpu_samples[prof_count - 1u],
                c_q3 - c_q1, cpu_total_raw);
#else
        fprintf(stderr,
                "CPU profile: previews=%u think_ms=%.3f think_cpu_ms=%.3f "
                "preview_ms(med=%.3f min=%.3f max=%.3f iqr=%.3f total=%.3f) "
                "preview_cpu_ms(med=%.3f min=%.3f max=%.3f iqr=%.3f total=%.3f)\n",
                prof_count, prof_think_ms, prof_think_cpu_ms,
                med, prof_samples[0], prof_samples[prof_count - 1u], q3 - q1, total,
                c_med * 1e-3, prof_cpu_samples[0] * 1e-3,
                prof_cpu_samples[prof_count - 1u] * 1e-3, (c_q3 - c_q1) * 1e-3,
                cpu_total_raw * 1e-3);
#endif
    }
    if (EnvFlag("KDC_CPU_PREVIEW_VERIFY")) {
        fprintf(stderr, "CPU verify: %u/%u candidates matched oracle ($6AEE + $B746..$CF45), mismatches=%u\n",
                verify_matched, verify_checked, verify_mismatches);
    }
}

static void Think(void)
{
    static int prof_on = -1;
    if (prof_on < 0) prof_on = EnvFlag("KDC_CPU_PROFILE");
    double think_t0 = prof_on ? NowMonoMs() : 0.0;
    double think_cpu0 = prof_on ? NowThreadCpuMs() : 0.0;
    if (!image) image = malloc(WRAM_SIZE);
    if (!snapshot) {
        snapshot_capacity = RtlRollbackSnapshotBound();
        snapshot = malloc(snapshot_capacity);
    }
    if (!image || !snapshot) { fputs("CPU planner: out of memory\n", stderr); abort(); }
    planning = true;
    size_t size = RtlRollbackSaveToMemory(snapshot, snapshot_capacity);
    if (!size) { fputs("CPU planner: snapshot unavailable\n", stderr); abort(); }
    if (!image_ready) {
        memcpy(image, g_cpu.ram, WRAM_SIZE);
        Prepare(image);
        image_ready = true;
    }
    for (unsigned n = 0; n < PREVIEWS_PER_FRAME && evaluated < queued; ++n) {
        Shot s = queue[evaluated++];
        ++previews;
        if (!Preview(&s)) continue;
        double score = Score(g_cpu.ram, &s);
        if (score > best_score) { best_score = score; best = s; }
    }
    bool restored = RtlRollbackLoadFromMemory(snapshot, size);
    planning = false;
    /* A failed restore would leave preview scratch in live guest state. */
    if (!restored) { fputs("CPU planner: rollback failed\n", stderr); abort(); }
    ++think_frames;
    if (prof_on) {
        prof_think_ms += NowMonoMs() - think_t0;
        prof_think_cpu_ms += NowThreadCpuMs() - think_cpu0;
    }
    if (evaluated < queued) return;
    if (!refined) Refine();
    else Decide();
}

void KdcCpuFrame(uint32_t *inputs)
{
    if (!difficulty) return;
    *inputs &= 0xfff; /* The physical second seat is CPU-owned. */
    if (phase == THINK && regs_ready) Think();
}

static void StartTurn(void)
{
    ++turn;
    phase = THINK;
    regs_ready = image_ready = false;
    queued = evaluated = previews = think_frames = 0;
    prof_count = verify_checked = verify_matched = verify_mismatches = 0;
    prof_think_ms = prof_think_cpu_ms = 0.0;
}

/* Turn the aim with the held and tapped Left/Right a person uses: one degree,
 * a pause, then a steady sweep while held, with taps to finish. */
static void Aim(uint8_t *r)
{
    if (!applied) {
        Put(r, 0xd9b6, best.loft);
        Put(r, 0xd9b8, best.spin_x);
        Put(r, 0xd9ba, best.spin_y);
        Put(r, 0x6ad6, best.loft ? 2 : 0);
        Put(r, 0x6ad8, best.loft ? 0 : 1);
        Put(r, 0x6ae0, 1); /* Redraw the guide for the chosen shot type. */
        applied = true;
    }
    int diff = ((int)best.angle - (int)(Word(r, 0xd9b4) % 360) + 540) % 360 - 180;
    unsigned pad = 0;
    if (diff) {
        unsigned toward = diff > 0 ? 0x0200 : 0x0100; /* Left raises the angle. */
        unsigned distance = (unsigned)abs(diff);
        if (distance >= 4 || (last_pad == toward && distance >= 3)) pad = toward;
        else pad = (tap++ & 1) ? 0 : toward;
    } else {
        phase = SETTLE;
        settle = 0;
    }
    last_pad = pad;
    Put(r, 0x67, pad);
    Put(r, 0x6b, 0);
}

static void KeepHandicap(uint8_t *r)
{
    unsigned level = KdcCpuHandicap(difficulty);
    Put(r, 0xecfd, level); /* Power */
    Put(r, 0xed01, level); /* Guide */
    Put(r, 0xed05, level); /* Spin marker */
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    uint8_t *r = cpu->ram;
    if (planning) {
        /* Inside a private preview only the look-ahead horizon applies. */
        if ((pc & 0x7fffff) == 0x01aa0e && !Word(r, 0x6ad6)) Put(r, 0x6ad2, horizon);
        return;
    }
    if (Match(r)) {
        switch (pc & 0x7fffff) {
        case 0x00f48c: score_wait = 0; break;
        case 0x03d495:
            /* The scorecard waits for the hole winner's controller, not P1.
             * Give the human two seconds to read a CPU win, then accept it. */
            if (Word(r, 0x6b84) == 1 && ++score_wait >= 120) Put(r, 0x51, Word(r, 0x51) | 0x80);
            break;
        /* $83:DD22 first draws the handicap grid; $83:DFC0 copies the
         * active player's column into play each turn. The CPU's column
         * shows, and plays under, its difficulty's settings. */
        case 0x03dd22:
        case 0x03dfc0: KeepHandicap(r); break;
        }
    }
    if (!OwnTurn(r)) return;
    switch (pc & 0x7fffff) {
    case 0x01a619: StartTurn(); break; /* Includes consecutive CPU turns. */
    case 0x01a680:
        if (phase == IDLE || phase == FLIGHT) StartTurn();
        if (phase == THINK) {
            if (!regs_ready) { regs = *cpu; regs_ready = true; }
            Put(r, 0x67, 0);
            Put(r, 0x6b, 0);
        } else if (phase == AIM) {
            Aim(r);
        } else if (phase == SETTLE) {
            Put(r, 0x67, 0);
            Put(r, 0x6b, ++settle > SETTLE_FRAMES ? 0x80 : 0);
            if (settle > SETTLE_FRAMES) phase = SPIN;
        }
        break;
    case 0x01bfa9: /* Spin selection: the same controls a person uses. */
        if (phase != SPIN) break;
        Put(r, 0x67, Word(r, 0x6b50) < best.spin_x ? 0x100 : Word(r, 0x6b50) > best.spin_x ? 0x200 : 0);
        Put(r, 0x6b, 0);
        break;
    case 0x01bfb5: {
        if (phase != SPIN) break;
        int y = (int)best.spin_y + spin_slip;
        if (y < 0) y = 0;
        if (y > 12) y = 12;
        if (Word(r, 0x6b50) == best.spin_x && Word(r, 0x6b52) == (unsigned)y) {
            Put(r, 0x6b, 0x80);
            phase = POWER;
        }
        break;
    }
    case 0x01c005: {
        if (phase != POWER) break;
        int target = (int)best.power + power_slip;
        if (target < 1) target = 1;
        if (target > (int)max_power) target = (int)max_power;
        Put(r, 0x6b, Word(r, 0x6b3a) >= (unsigned)target ? 0x80 : 0);
        break;
    }
    case 0x01c04c: phase = FLIGHT; break;
    }
}

void KdcCpuInitialize(void)
{
    static const uint32_t sites[] = {0x81a619, 0x81a680, 0x81aa0e, 0x81bfa9, 0x81bfb5, 0x81c005,
                                     0x81c04c, 0x80f48c, 0x83d495, 0x83dd22, 0x83dfc0};
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i)
        if (!interp_bridge_add_pre_opcode_hook(sites[i], Observe)) {
            fputs("Unable to register CPU opponent\n", stderr);
            abort();
        }
}
