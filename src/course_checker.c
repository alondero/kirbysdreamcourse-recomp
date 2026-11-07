#include "course_checker.h"

typedef struct Candidate { KdcCheckerEdge edge; uint64_t mask; } Candidate;

/* Compile-time half-plane masks. Bit (8*y+x) corresponds to the integer
 * source centre (x,y); the threshold is t+1/2. No runtime construction,
 * mutable initialization, or per-fit candidate stack is needed. */
#define CUT(s, t, y) ((t) + 1 - (s) * (y))
#define ROW(s, t, y) ((CUT(s, t, y) <= 0 ? UINT64_C(255) : \
                      CUT(s, t, y) >= 8 ? UINT64_C(0) : \
                      (UINT64_C(255) << (CUT(s, t, y) & 7)) & 255) << (8 * (y)))
#define MASK(s, t) (ROW(s,t,0) | ROW(s,t,1) | ROW(s,t,2) | ROW(s,t,3) | \
                    ROW(s,t,4) | ROW(s,t,5) | ROW(s,t,6) | ROW(s,t,7))
#define EDGE(s, t) {{s, 2*(t)+1, 0}, MASK(s,t)}, {{s, 2*(t)+1, 1}, ~MASK(s,t)}
static const Candidate candidates[] = {
    EDGE(-2, -20), EDGE(-2, -19), EDGE(-2, -18), EDGE(-2, -17),
    EDGE(-2, -16), EDGE(-2, -15), EDGE(-2, -14), EDGE(-2, -13),
    EDGE(-2, -12), EDGE(-2, -11), EDGE(-2, -10), EDGE(-2, -9),
    EDGE(-2, -8), EDGE(-2, -7), EDGE(-2, -6), EDGE(-2, -5),
    EDGE(-2, -4), EDGE(-2, -3), EDGE(-2, -2), EDGE(-2, -1),
    EDGE(-2, 0), EDGE(-2, 1), EDGE(-2, 2), EDGE(-2, 3),
    EDGE(-2, 4), EDGE(-2, 5), EDGE(-2, 6), EDGE(-2, 7),
    EDGE(-2, 8), EDGE(-2, 9), EDGE(-2, 10), EDGE(-2, 11),
    EDGE(-2, 12),
    EDGE(2, -6), EDGE(2, -5), EDGE(2, -4), EDGE(2, -3),
    EDGE(2, -2), EDGE(2, -1), EDGE(2, 0), EDGE(2, 1),
    EDGE(2, 2), EDGE(2, 3), EDGE(2, 4), EDGE(2, 5),
    EDGE(2, 6), EDGE(2, 7), EDGE(2, 8), EDGE(2, 9),
    EDGE(2, 10), EDGE(2, 11), EDGE(2, 12), EDGE(2, 13),
    EDGE(2, 14), EDGE(2, 15), EDGE(2, 16), EDGE(2, 17),
    EDGE(2, 18), EDGE(2, 19), EDGE(2, 20), EDGE(2, 21),
    EDGE(2, 22), EDGE(2, 23), EDGE(2, 24), EDGE(2, 25),
    EDGE(2, 26),
    EDGE(-1, -13), EDGE(-1, -12), EDGE(-1, -11), EDGE(-1, -10),
    EDGE(-1, -9), EDGE(-1, -8), EDGE(-1, -7), EDGE(-1, -6),
    EDGE(-1, -5), EDGE(-1, -4), EDGE(-1, -3), EDGE(-1, -2),
    EDGE(-1, -1), EDGE(-1, 0), EDGE(-1, 1), EDGE(-1, 2),
    EDGE(-1, 3), EDGE(-1, 4), EDGE(-1, 5), EDGE(-1, 6),
    EDGE(-1, 7), EDGE(-1, 8), EDGE(-1, 9), EDGE(-1, 10),
    EDGE(-1, 11), EDGE(-1, 12),
    EDGE(1, -6), EDGE(1, -5), EDGE(1, -4), EDGE(1, -3),
    EDGE(1, -2), EDGE(1, -1), EDGE(1, 0), EDGE(1, 1),
    EDGE(1, 2), EDGE(1, 3), EDGE(1, 4), EDGE(1, 5),
    EDGE(1, 6), EDGE(1, 7), EDGE(1, 8), EDGE(1, 9),
    EDGE(1, 10), EDGE(1, 11), EDGE(1, 12), EDGE(1, 13),
    EDGE(1, 14), EDGE(1, 15), EDGE(1, 16), EDGE(1, 17),
    EDGE(1, 18), EDGE(1, 19),
};
#undef EDGE
#undef MASK
#undef ROW
#undef CUT

static bool Side(KdcCheckerEdge edge, double x, double y)
{
    /* Threshold is twice the boundary in integer pixel-centre coordinates.
     * The half-texel shift makes the phase independent of tile boundaries. */
    bool side = 2 * ((x - 0.5) + edge.slope * (y - 0.5)) >= edge.threshold;
    return side != (edge.reverse != 0);
}

uint8_t KdcCheckerSample(const KdcChecker *checker, double x, double y)
{
    bool a = Side(checker->edge[0], x, y);
    bool b = Side(checker->edge[1], x, y);
    bool value = checker->operation == 0 ? a :
                 checker->operation == 1 ? a && b : a != b;
    return checker->colors[value];
}

/* Extra centres reveal corners hidden beyond the original 8x8 samples. */
static bool BorderMatches(const KdcChecker *checker, const uint8_t border[kKdcCheckerBorderSize])
{
    for (int k = 0; k < kKdcCheckerBorderSize; ++k) {
        int x, y;
        KdcCheckerBorderPosition(k, &x, &y);
        if (KdcCheckerSample(checker, x + 0.5, y + 0.5) != border[k]) return false;
    }
    return true;
}

static bool Classify(const uint16_t words[16], uint8_t colors[2], uint64_t *target)
{
    colors[0] = colors[1] = 0;
    *target = 0;
    unsigned count = 0;
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            unsigned shift = 7 - x;
            unsigned p = ((words[y] >> shift) & 1) |
                         ((words[y] >> (shift + 7)) & 2) |
                         ((words[y + 8] >> shift) & 1) << 2 |
                         ((words[y + 8] >> (shift + 7)) & 2) << 2;
            if (!p) return false;
            if (!colors[0]) colors[0] = (uint8_t)p;
            if (p == colors[0]) continue;
            if (!colors[1]) colors[1] = (uint8_t)p;
            if (p != colors[1]) return false;
            *target |= UINT64_C(1) << (8 * y + x);
            ++count;
        }
    /* Small isolated marks do not establish a checker edge. */
    if (count < 8 || count > 56) return false;
    return true;
}

bool KdcCheckerNeedsContext(const uint16_t words[16])
{
    uint8_t colors[2];
    uint64_t target;
    return Classify(words, colors, &target);
}

bool KdcCheckerFit(const uint16_t words[16], const uint8_t border[kKdcCheckerBorderSize], KdcChecker *checker)
{
    uint8_t colors[2];
    uint64_t target;
    if (!Classify(words, colors, &target)) return false;
    if (!border) return false;
    for (unsigned k = 0; k < kKdcCheckerBorderSize; ++k)
        if (border[k] != colors[0] && border[k] != colors[1]) return false;
    const unsigned n = sizeof(candidates) / sizeof(*candidates);
    *checker = (KdcChecker){.colors = {colors[0], colors[1]}};
    bool found = false;
    for (unsigned i = 0; i < n; ++i)
        if (candidates[i].mask == target) {
            KdcChecker model = *checker;
            model.edge[0] = candidates[i].edge;
            if (!BorderMatches(&model, border)) continue;
            if (found) return false;
            *checker = model;
            found = true;
        }
    if (found) return true;
    for (unsigned i = 0; i < n; ++i)
        for (unsigned j = i + 1; j < n; ++j) {
            if (candidates[i].edge.slope == candidates[j].edge.slope) continue;
            uint64_t a = candidates[i].mask, b = candidates[j].mask;
            /* Try both operations: a native intersection match can fail
             * the border while an XOR using the same edges still matches. */
            for (unsigned op = 1; op <= 2; ++op) {
                bool inverted_corner = op == 1 && (a & b) == ~target;
                if (op == 1 && (a & b) != target && !inverted_corner) continue;
                if (op == 2 && ((a ^ b) != target || candidates[i].edge.reverse)) continue;
                /* XOR is unchanged when both sides are inverted; the check
                 * above retains one representative before uniqueness testing.
                 * A corner's exterior needs swapped colours, not a different
                 * geometric model, when the first pixel is inside it. */
                KdcChecker model = {.colors = {colors[inverted_corner], colors[!inverted_corner]},
                    .edge = {candidates[i].edge, candidates[j].edge}, .operation = (uint8_t)op};
                if (!BorderMatches(&model, border)) continue;
                if (found) return false;
                *checker = model;
                found = true;
            }
        }
    return found;
}
