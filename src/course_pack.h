#ifndef KDC_COURSE_PACK_H
#define KDC_COURSE_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    KDC_PACK_HOLES_PER_COURSE = 8,
    KDC_PACK_STREAMS = 11,
    KDC_PACK_MAX_PACKS = 16,
    KDC_PACK_MAX_NAME = 32,
};

typedef struct {
    bool external;
    uint8_t rom_map;
    uint8_t music_id;
    uint32_t stream_offset[KDC_PACK_STREAMS];
    uint32_t stream_size[KDC_PACK_STREAMS];
} KdcPackHole;

typedef struct {
    char name[24];
    uint8_t palette;
    uint8_t anim_palette;
    uint8_t theme;
    KdcPackHole holes[KDC_PACK_HOLES_PER_COURSE];
} KdcPackCourse;

typedef struct {
    char id[24];
    char name[KDC_PACK_MAX_NAME];
    char short_name[16];
    bool external;
    unsigned course_count;
    KdcPackCourse *courses;
    uint8_t *storage;
    size_t storage_size;
} KdcCoursePack;

/* Exact C11 implementation of the USA HAL-LZSS decompressor ($80:9F18).
 * Returns true on success (terminated by 0xFF within bounds) and writes the
 * decompressed byte count to *out_size and compressed byte count to
 * *out_consumed when non-NULL. */
bool KdcHalDecompress(const uint8_t *src, size_t src_size,
                      uint8_t *dst, size_t dst_cap,
                      size_t *out_size, size_t *out_consumed);

/* Catalog lifecycle: built-in 1P Normal/Extra packs plus optional external
 * IPS packs discovered in courses/ or supplied via --course-pack. */
void KdcCoursePacksResetCatalog(void);
void KdcCoursePacksEnsureLoaded(void);
void KdcCoursePacksScanDirectory(const char *dir_path);
bool KdcCoursePackLoadIpsMemory(const uint8_t *ips, size_t ips_size,
                                const char *label,
                                char *error_buf, size_t error_size);
bool KdcCoursePackLoadIpsFile(const char *path,
                              char *error_buf, size_t error_size);
bool KdcCoursePacksAddCliPath(const char *path);

unsigned KdcCoursePackCount(void);
unsigned KdcCoursePackExternalCount(void);
const KdcCoursePack *KdcCoursePackGet(unsigned index);
int KdcCoursePackFindById(const char *id);

/* Synced with launcher setting (kdc-courses.cfg) or forced on by CLI. */
void KdcCoursePacksSetEnabled(bool enabled);
bool KdcCoursePacksMenuEnabled(void);
void KdcCoursePacksDescribeStatus(char *out, size_t out_size);

/* Active custom course state and USA runtime injection hooks. */
bool KdcCoursePackSelect(unsigned pack_index, unsigned course_index);
void KdcCoursePackClearActive(void);
bool KdcCoursePackHasActive(void);
bool KdcCoursePackActiveIsExternal(void);
const KdcCoursePack *KdcCoursePackActivePack(void);
const KdcPackCourse *KdcCoursePackActiveCourse(void);
uint32_t KdcCoursePackGeneration(void);
void KdcCoursePackBumpGeneration(void);

/* Implemented in course_pack_runtime.c (requires SNESRecomp runtime headers). */
void KdcCoursePackInitialize(void);

#endif
