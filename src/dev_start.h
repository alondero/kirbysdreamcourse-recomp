#ifndef KDC_DEV_START_H
#define KDC_DEV_START_H

/* Removes game-owned developer arguments before the shared host parses argv.
 * Returns 0 to launch, 1 after printing help, or -1 on invalid arguments. */
int KdcDevParseArgs(int *argc, char **argv);
void KdcDevInitialize(void);
void KdcDevReset(void);
void KdcDevStateLoaded(void);

#endif
