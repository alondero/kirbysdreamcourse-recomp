#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned hd_reads, source_reads;
extern char *__real_getenv(const char *name);

static void check_reads(void)
{
    if (hd_reads != 1) {
        fprintf(stderr, "KDC_HD environment reads: expected 1, got %u\n", hd_reads);
        _Exit(93);
    }
    if (source_reads != 1) {
        fprintf(stderr, "KDC_HD_SOURCE environment reads: expected 1, got %u\n", source_reads);
        _Exit(94);
    }
}

__attribute__((constructor)) static void install_check(void)
{
    atexit(check_reads);
}

char *__wrap_getenv(const char *name)
{
    if (!strcmp(name, "KDC_HD")) {
        if (++hd_reads > 1) {
            fprintf(stderr, "KDC_HD reread on repeated HD scale query\n");
            exit(93);
        }
    }
    if (!strcmp(name, "KDC_HD_SOURCE")) {
        if (++source_reads > 1) {
            fprintf(stderr, "KDC_HD_SOURCE reread on repeated HD scale query\n");
            exit(94);
        }
    }
    return __real_getenv(name);
}
