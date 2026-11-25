/* Link against the real bridge test with -Wl,--wrap=getenv. The ordinary
 * bridge suite exercises repeated/nested entries, observers and redirects.
 * Catch repeated diagnostic lookups at that seam, rather than time a mock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned reads, handoff_reads;
extern char *__real_getenv(const char *name);

static void check_reads(void)
{
    if (reads != 1) {
        fprintf(stderr, "WLOG_STATE environment reads: expected 1, got %u\n", reads);
        _Exit(91);
    }
    if (handoff_reads != 1) {
        fprintf(stderr, "APU_HANDOFF environment reads: expected 1, got %u\n", handoff_reads);
        _Exit(92);
    }
}

__attribute__((constructor)) static void install_check(void)
{
    atexit(check_reads);
}

char *__wrap_getenv(const char *name)
{
    if (!strcmp(name, "SNESRECOMP_WLOG_STATE")) {
        if (++reads > 1) {
            fprintf(stderr, "WLOG_STATE reread on repeated bridge entry\n");
            exit(91);
        }
    }
    if (!strcmp(name, "SNESRECOMP_APU_HANDOFF")) {
        if (++handoff_reads > 1) {
            fprintf(stderr, "APU_HANDOFF reread on repeated generated-code bounce\n");
            exit(92);
        }
    }
    return __real_getenv(name);
}
