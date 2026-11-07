#include "course_checker.h"

typedef struct Candidate { KdcCheckerEdge edge; uint64_t mask; } Candidate;

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
    Candidate candidates[236];
    unsigned n = 0;
    static const int slopes[] = {-2, 2, -1, 1};
    for (unsigned s = 0; s < 4; ++s) {
        int slope = slopes[s], low = slope < 0 ? 7 * slope : 0;
        int high = 7 + (slope > 0 ? 7 * slope : 0);
        for (int t = low - 6; t < high + 6; ++t) {
            KdcCheckerEdge edge = {slope, 2 * t + 1, 0};
            uint64_t mask = 0;
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x)
                    if (Side(edge, x + 0.5, y + 0.5))
                        mask |= UINT64_C(1) << (8 * y + x);
            for (int reverse = 0; reverse < 2; ++reverse) {
                edge.reverse = reverse;
                candidates[n++] = (Candidate){edge, reverse ? ~mask : mask};
            }
        }
    }
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
            unsigned op = (a & b) == target ? 1 : (a ^ b) == target ? 2 : 0;
            if (!op) continue;
            /* XOR is unchanged when both sides are inverted. Keep only
             * one representative before testing uniqueness. */
            if (op == 2 && candidates[i].edge.reverse) continue;
            /* Native samples can hide a corner's boundary by a whole
             * subpixel. Never choose an arbitrary first fit: adjacent
             * tiles could then reconstruct different phases. */
            KdcChecker model = {.colors = {colors[0], colors[1]},
                .edge = {candidates[i].edge, candidates[j].edge}, .operation = (uint8_t)op};
            if (!BorderMatches(&model, border)) continue;
            if (found) return false;
            *checker = model;
            found = true;
        }
    return found;
}
