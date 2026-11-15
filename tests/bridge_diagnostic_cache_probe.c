/* Link against the real bridge test with -Wl,--wrap=getenv. The ordinary
 * bridge suite exercises repeated/nested entries, observers and redirects.
 * Catch repeated diagnostic lookups at that seam, rather than time a mock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned reads;
extern char *__real_getenv(const char *name);

static void check_reads(void)
{
    if (reads != 1) {
        fprintf(stderr, "WLOG_STATE environment reads: expected 1, got %u\n", reads);
        _Exit(91);
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
    return __real_getenv(name);
}
