#ifndef KDC_PRACTICE_H
#define KDC_PRACTICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct Ppu;
typedef struct {
    int screen; /* 0: game, 1: hole picker, 2: practice pause */
    int row, mode, course, hole;
    bool active, title;
    const char *error;
} KdcPracticeView;
const KdcPracticeView *KdcPracticeCurrentView(void);
void KdcPracticeInitialize(void);
bool KdcPracticeFrame(uint32_t *inputs);
bool KdcPracticeSnapshotsAllowed(void);
void KdcPracticeReset(void);
void KdcPracticeStateLoaded(void);
void KdcPracticeCaptureFont(const struct Ppu *ppu);
bool KdcPracticeVisible(void);
void KdcPracticeDraw(uint8_t *dst, size_t pitch, int width, int height);
void KdcPracticeDrawHd(uint8_t *dst, size_t pitch, int width, int height, unsigned scale);
#endif
