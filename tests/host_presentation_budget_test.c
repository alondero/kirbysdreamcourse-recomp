#include "desktop/host_clock.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

/* Synthetic wall time runs the host's two dispatch sites with the measured
 * issue #67 costs. No ROM or machine-speed threshold is involved. */
static void replay(double guest, double present, double rate)
{
    SnesHostClock clock;
    snes_host_clock_reset(&clock, 0, SNES_HOST_NTSC_HZ, rate);
    double now = 0;
    while (now < 10) {
        if (!snes_host_clock_simulation_due(&clock, now)) {
            if (snes_host_clock_presentation_allowed(&clock, now, true, false)) {
                snes_host_clock_presentation_finished(&clock, now, now + present);
                now += present;
            }
            double next = snes_host_clock_budget_deadline(&clock, now);
            if (now < next) now = next;
            continue;
        }
        now += guest;
        snes_host_clock_decoupled_simulation_done(&clock, now, false, 1);
        if (snes_host_clock_presentation_allowed(&clock, now, false, false)) {
            snes_host_clock_presentation_finished(&clock, now, now + present);
            now += present;
        }
    }
    printf("guest=%.3f present=%.3f target=%.0f: simulations=%llu presentations=%llu\n",
           guest, present, rate, (unsigned long long)clock.simulation_frames,
           (unsigned long long)clock.presentations);
    fflush(stdout);
    assert(clock.simulation_frames >= 599);
    assert(clock.simulation_frames <= 602);
    assert(clock.presentations > 50);
    if (guest + present < .004) assert(clock.presentations >= 1190);
}

static void audio_paced_replay(double guest, double present, unsigned minimum_presents)
{
    SnesHostClock clock;
    snes_host_clock_reset(&clock, 0, SNES_HOST_NTSC_HZ, 120);
    double now = .030;
    while (now < 10) {
        if (!snes_host_clock_simulation_due(&clock, now)) {
            if (snes_host_clock_presentation_allowed(&clock, now, true, false)) {
                snes_host_clock_presentation_finished(&clock, now, now + present);
                now += present;
            }
            double next = snes_host_clock_budget_deadline(&clock, now);
            if (now < next) now = next;
            continue;
        }
        /* A cheap scene must still interpolate when an independent audio
         * consumer runs slightly slower than the SNES wall clock. */
        double consumer = .030 + clock.simulation_frames / 60.0;
        double waited = fmax(0, consumer - now);
        snes_host_clock_consumer_paced(&clock, now, waited);
        now += waited + guest;
        snes_host_clock_decoupled_simulation_done(&clock, now, false, 1);
        if (snes_host_clock_presentation_allowed(&clock, now, false, waited > 0)) {
            snes_host_clock_presentation_finished(&clock, now, now + present);
            now += present;
        }
    }
    printf("audio paced: simulations=%llu presentations=%llu\n",
           (unsigned long long)clock.simulation_frames, (unsigned long long)clock.presentations);
    fflush(stdout);
    assert(clock.simulation_frames >= 595);
    assert(clock.simulation_frames <= 602);
    assert(clock.presentations >= minimum_presents);
}

int main(void)
{
    replay(.0125, .011, 120);
    replay(.0125, .011, 60);
    replay(.002, .001, 120);
    audio_paced_replay(.003, .001, 1180);
    audio_paced_replay(.0125, .011, 150);
    SnesHostClock clock;
    snes_host_clock_reset(&clock, 0, 60, 120);
    snes_host_clock_decoupled_simulation_done(&clock, .020, false, 2);
    assert(fabs(clock.next_simulation - 2.0 / 60) < 1e-9);
    /* Late simulation takes precedence; stall recovery stays bounded. */
    assert(!snes_host_clock_presentation_allowed(&clock, .040, false, false));
    assert(snes_host_clock_presentation_allowed(&clock, .110, false, false));
    assert(!snes_host_clock_presentation_allowed(&clock, .110, true, false));
    snes_host_clock_decoupled_simulation_done(&clock, 2, false, 1);
    assert(clock.next_simulation == 2);
    snes_host_clock_reset(&clock, 0, 60, 120);
    snes_host_clock_decoupled_simulation_done(&clock, 2, true, 1);
    assert(fabs(clock.next_simulation - 1.0 / 60) < 1e-9);
    /* Legacy lockstep debt policy remains available and unchanged. */
    snes_host_clock_simulation_done(&clock, 2, false, 1);
    assert(fabs(clock.next_simulation - (2 + 1.0 / 60)) < 1e-9);
    snes_host_clock_reset(&clock, 0, 60, 120);
    snes_host_clock_decoupled_simulation_done(&clock, .012, false, 1);
    snes_host_clock_presentation_finished(&clock, .012, .023);
    assert(!snes_host_clock_presentation_allowed(&clock, .024, true, false));
    assert(snes_host_clock_presentation_allowed(&clock, .024, false, true));
    assert(!snes_host_clock_presentation_allowed(&clock, .024, true, true));
    assert(clock.missed_presentations == 1);
    assert(clock.presentation_cost > .0109);
    assert(snes_host_clock_budget_deadline(&clock, .024) == clock.next_simulation);
    return 0;
}
