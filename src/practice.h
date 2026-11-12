#ifndef KDC_PRACTICE_H
#define KDC_PRACTICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct Ppu;
typedef struct {
    int screen; /* 0: game, 1: hole picker, 2: session pause */
    int row, mode, course, hole, ability;
    int cpu; /* 0: practice, 1..3: CPU match difficulty (original 2P menus) */
    bool active, title, title_background;
    const char *error;
} KdcPracticeView;
enum { KDC_PRACTICE_ABILITIES = 11 };
/* Returned by KdcPracticeGuestGlyph for characters the guest's title-menu
 * font sheet does not carry. */
#define KDC_PRACTICE_NO_GLYPH 0xffffffffu
const char *KdcPracticeAbilityName(unsigned ability);
unsigned KdcPracticeGuestGlyph(char c);
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
