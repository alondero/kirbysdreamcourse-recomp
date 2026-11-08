/* The checker fit must stay a pure function of its inputs while getting
 * cheaper: the orbit view fits every newly visible terrain location, and a
 * hole load fits thousands of characters in one frame. ReferenceFit is the
 * original exhaustive pair search (logic unchanged, comments trimmed), used
 * as the oracle. Inputs are synthetic (models sampled from the candidate set,
 * checker phases and random two-colour patterns), not game data. Wall time
 * is compared only with --timing, which CI does not pass. */
#include "../src/course_checker.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool ReferenceFit(const uint16_t words[16], const uint8_t border[kKdcCheckerBorderSize],
                         KdcChecker *checker)
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
            for (unsigned op = 1; op <= 2; ++op) {
                bool inverted_corner = op == 1 && (a & b) == ~target;
                if (op == 1 && (a & b) != target && !inverted_corner) continue;
                if (op == 2 && ((a ^ b) != target || candidates[i].edge.reverse)) continue;
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

enum { kCases = 6000 };
static uint16_t words[kCases][16];
static uint8_t borders[kCases][kKdcCheckerBorderSize];
static unsigned cases;
static uint32_t rng = 0x2468ace1u;

static uint32_t Next(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void Add(const uint8_t pixels[64], const uint8_t border[kKdcCheckerBorderSize])
{
    assert(cases < kCases);
    memset(words[cases], 0, sizeof(words[cases]));
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            for (unsigned bit = 0; bit < 4; ++bit)
                words[cases][y + 8 * (bit / 2)] |= (uint16_t)
                    (((pixels[8 * y + x] >> bit) & 1) << (7 - x + 8 * (bit % 2)));
    memcpy(borders[cases], border, kKdcCheckerBorderSize);
    ++cases;
}

/* Sample a model at every pixel centre and border centre. */
static void AddModel(const KdcChecker *model, bool perturb)
{
    uint8_t pixels[64], border[kKdcCheckerBorderSize];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixels[8 * y + x] = KdcCheckerSample(model, x + 0.5, y + 0.5);
    for (int k = 0; k < kKdcCheckerBorderSize; ++k) {
        int x, y;
        KdcCheckerBorderPosition(k, &x, &y);
        border[k] = KdcCheckerSample(model, x + 0.5, y + 0.5);
    }
    if (perturb) border[Next() % kKdcCheckerBorderSize] ^= model->colors[0] ^ model->colors[1];
    Add(pixels, border);
}

static void BuildCorpus(void)
{
    const unsigned n = sizeof(candidates) / sizeof(*candidates);
    static const uint8_t palettes[][2] = {{3, 12}, {12, 3}, {1, 15}, {7, 9}};
    /* Single edges and both pair operations, with exact and perturbed borders. */
    while (cases < 4000) {
        const uint8_t *colors = palettes[Next() % 4];
        KdcChecker model = {.colors = {colors[0], colors[1]}};
        model.edge[0] = candidates[Next() % n].edge;
        model.edge[1] = candidates[Next() % n].edge;
        model.operation = (uint8_t)(Next() % 3);
        AddModel(&model, Next() % 4 == 0);
    }
    /* Irregular two-colour artwork, which must be rejected after a full search. */
    while (cases < kCases) {
        uint8_t pixels[64], border[kKdcCheckerBorderSize];
        for (unsigned i = 0; i < 64; ++i) pixels[i] = Next() % 3 ? 5 : 11;
        for (unsigned i = 0; i < kKdcCheckerBorderSize; ++i) border[i] = Next() % 2 ? 5 : 11;
        Add(pixels, border);
    }
}

static bool Same(const KdcChecker *a, const KdcChecker *b)
{
    for (unsigned e = 0; e < 2; ++e)
        if (a->edge[e].slope != b->edge[e].slope || a->edge[e].threshold != b->edge[e].threshold ||
            a->edge[e].reverse != b->edge[e].reverse)
            return false;
    return a->colors[0] == b->colors[0] && a->colors[1] == b->colors[1] &&
           a->operation == b->operation;
}

static double Seconds(bool (*fit)(const uint16_t *, const uint8_t *, KdcChecker *), unsigned *accepted)
{
    clock_t start = clock();
    *accepted = 0;
    for (unsigned repeat = 0; repeat < 3; ++repeat)
        for (unsigned i = 0; i < cases; ++i) {
            KdcChecker model;
            *accepted += fit(words[i], borders[i], &model);
        }
    return (double)(clock() - start) / CLOCKS_PER_SEC;
}

int main(int argc, char **argv)
{
    BuildCorpus();
    unsigned accepted = 0, single = 0, pair = 0;
    for (unsigned i = 0; i < cases; ++i) {
        KdcChecker want, got;
        memset(&want, 0, sizeof(want));
        memset(&got, 0, sizeof(got));
        bool expected = ReferenceFit(words[i], borders[i], &want);
        bool actual = KdcCheckerFit(words[i], borders[i], &got);
        if (expected != actual || (expected && !Same(&want, &got))) {
            fprintf(stderr, "case %u: reference=%d fit=%d\n", i, expected, actual);
            return 1;
        }
        accepted += expected;
        if (expected) ++*(want.operation ? &pair : &single);
    }
    /* The corpus must exercise every accepted shape and many rejections. */
    assert(single > 100 && pair > 100 && accepted < cases - 1000);
    printf("checker fit: %u cases, %u accepted (%u single, %u pair)\n", cases, accepted, single, pair);
    if (argc < 2 || strcmp(argv[1], "--timing")) return 0;
    unsigned reference_accepted, fit_accepted;
    double reference = Seconds(ReferenceFit, &reference_accepted);
    double fast = Seconds(KdcCheckerFit, &fit_accepted);
    assert(reference_accepted == fit_accepted);
    printf("reference %.3fs, fit %.3fs\n", reference, fast);
    /* A relative bound, measured in one process, tolerates machine speed but
     * not a loaded machine; clock() is wall time on Windows, CPU elsewhere. */
    if (fast * 4 > reference) {
        fprintf(stderr, "fit is not at least 4x faster than the exhaustive search\n");
        return 1;
    }
    return 0;
}
