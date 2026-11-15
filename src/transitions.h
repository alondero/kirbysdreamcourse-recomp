#pragma once

/* Host pacing only: never changes guest input, timers or control flow. */
void KdcTransitionsInitialize(void);
void KdcTransitionsReset(void);
void KdcTransitionsFrame(void);
int KdcTransitionsSpeed(void);
