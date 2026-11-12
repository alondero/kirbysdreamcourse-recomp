#ifndef KDC_DEV_START_H
#define KDC_DEV_START_H

/* Removes game-owned developer arguments before the shared host parses argv.
 * Returns 0 to launch, 1 after printing help, or -1 on invalid arguments. */
int KdcDevParseArgs(int *argc, char **argv);
void KdcDevInitialize(void);
void KdcDevReset(void);
void KdcDevStateLoaded(void);
/* Runtime practice uses the same validated start and reset hooks as the CLI.
 * NULL disables the shortcut. Invalid selections leave it unchanged. */
int KdcDevSelect(const char *selector);
int KdcDevEnabled(void);

#endif
