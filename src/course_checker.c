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
enum { kCandidates = sizeof(candidates) / sizeof(*candidates) };
_Static_assert(kCandidates < 256, "candidate indices are stored in bytes");

/* Candidate indices ordered by mask (then index), so an XOR partner is a
 * binary search rather than a scan. Built once; the order is a pure
 * function of the constant table. */
static uint8_t by_mask[kCandidates];
static bool by_mask_ready;

static void SortCandidatesByMask(void)
{
    if (by_mask_ready) return;
    for (unsigned i = 0; i < kCandidates; ++i) {
        unsigned at = i;
        while (at > 0 && candidates[by_mask[at - 1]].mask > candidates[i].mask) {
            by_mask[at] = by_mask[at - 1];
            --at;
        }
        by_mask[at] = (uint8_t)i;
    }
    by_mask_ready = true;
}

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

/* Keeps a model that matches the border; false once a second one matches,
 * because an ambiguous tile has no single geometric reading. */
static bool KeepIfUnique(const KdcChecker *model, const uint8_t border[kKdcCheckerBorderSize],
                         KdcChecker *checker, bool *found)
{
    if (!BorderMatches(model, border)) return true;
    if (*found) return false;
    *checker = *model;
    *found = true;
    return true;
}

bool KdcCheckerFit(const uint16_t words[16], const uint8_t border[kKdcCheckerBorderSize], KdcChecker *checker)
{
    uint8_t colors[2];
    uint64_t target;
    if (!Classify(words, colors, &target)) return false;
    if (!border) return false;
    for (unsigned k = 0; k < kKdcCheckerBorderSize; ++k)
        if (border[k] != colors[0] && border[k] != colors[1]) return false;
    *checker = (KdcChecker){.colors = {colors[0], colors[1]}};
    bool found = false;
    for (unsigned i = 0; i < kCandidates; ++i)
        if (candidates[i].mask == target) {
            KdcChecker model = *checker;
            model.edge[0] = candidates[i].edge;
            if (!KeepIfUnique(&model, border, checker, &found)) return false;
        }
    if (found) return true;
    /* Pairs i < j of nonparallel edges. The result requires a unique model,
     * so the search order does not matter; only the candidates are pruned.
     * Intersection: both masks contain the corner (or its complement, whose
     * exterior swaps colours when the first pixel is inside the corner). */
    SortCandidatesByMask();
    for (unsigned inverted = 0; inverted < 2; ++inverted) {
        const uint64_t goal = inverted ? ~target : target;
        /* Many candidates share a mask (thresholds beyond the tile give the
         * empty or full mask), so test each pair of distinct masks once. */
        uint8_t member[kCandidates], start[kCandidates + 1];
        unsigned members = 0, groups = 0;
        for (unsigned k = 0; k < kCandidates; ++k) {
            const unsigned i = by_mask[k];
            if ((candidates[i].mask & goal) != goal) continue;
            if (!members || candidates[member[members - 1]].mask != candidates[i].mask)
                start[groups++] = (uint8_t)members;
            member[members++] = (uint8_t)i;
        }
        start[groups] = (uint8_t)members;
        for (unsigned g = 0; g < groups; ++g)
            for (unsigned h = g; h < groups; ++h) {
                if ((candidates[member[start[g]]].mask & candidates[member[start[h]]].mask) != goal)
                    continue;
                for (unsigned p = start[g]; p < start[g + 1]; ++p)
                    for (unsigned q = g == h ? p + 1 : start[h]; q < start[h + 1]; ++q) {
                        const bool ordered = member[p] < member[q];
                        const unsigned i = ordered ? member[p] : member[q];
                        const unsigned j = ordered ? member[q] : member[p];
                        if (candidates[i].edge.slope == candidates[j].edge.slope) continue;
                        KdcChecker model = {.colors = {colors[inverted], colors[!inverted]},
                            .edge = {candidates[i].edge, candidates[j].edge}, .operation = 1};
                        if (!KeepIfUnique(&model, border, checker, &found)) return false;
                    }
            }
    }
    /* XOR: the partner's mask is determined. XOR is unchanged when both sides
     * are inverted, so the first edge is never reversed: one representative
     * survives to uniqueness testing. An intersection match can fail the
     * border while an XOR of the same edges still matches. */
    for (unsigned i = 0; i < kCandidates; ++i) {
        if (candidates[i].edge.reverse) continue;
        const uint64_t want = candidates[i].mask ^ target;
        unsigned low = 0, high = kCandidates;
        while (low < high) {
            unsigned middle = (low + high) / 2;
            if (candidates[by_mask[middle]].mask < want) low = middle + 1;
            else high = middle;
        }
        for (; low < kCandidates && candidates[by_mask[low]].mask == want; ++low) {
            const unsigned j = by_mask[low];
            if (j <= i || candidates[i].edge.slope == candidates[j].edge.slope) continue;
            KdcChecker model = {.colors = {colors[0], colors[1]},
                .edge = {candidates[i].edge, candidates[j].edge}, .operation = 2};
            if (!KeepIfUnique(&model, border, checker, &found)) return false;
        }
    }
    return found;
}
