#include "course_camera.h"
#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define CLOSE(a,b) (fabs((a)-(b)) < 0.00001)

int main(void)
{
    KdcCourseCamera camera;
    KdcCameraReset(&camera);
    CHECK(!camera.active && camera.zoom == 1);
    KdcCameraStep(&camera, 3000, -3000, false, 1.0 / 60);
    CHECK(!camera.active && camera.yaw == 0 && camera.zoom == 1);
    /* The original isometric projection is the zero-angle basis. */
    KdcCameraPoint p = KdcCameraProject(&camera, 1, 0, 1, 256, 224);
    CHECK(CLOSE(p.x, 160) && CLOSE(p.y, 112));
    for (int i = 0; i < 60; ++i)
        KdcCameraStep(&camera, -32768, 0, false, 1.0 / 60);
    CHECK(camera.active);
    p = KdcCameraProject(&camera, 1, 0, 1, 256, 224);
    CHECK(CLOSE(p.x, 160) && CLOSE(p.y, 80));
    for (int i = 0; i < 60; ++i)
        KdcCameraStep(&camera, 0, -32768, false, 1.0 / 60);
    CHECK(CLOSE(camera.zoom, 2));
    camera.target_x = 4; camera.target_y = 7; camera.target_z = 2;
    p = KdcCameraProject(&camera, 4, 7, 2, 800, 224);
    CHECK(CLOSE(p.x, 400) && CLOSE(p.y, 112));
    for (int i = 0; i < 600; ++i)
        KdcCameraStep(&camera, 0, -32768, false, 1.0 / 60);
    CHECK(camera.zoom == 4);
    for (int i = 0; i < 600; ++i)
        KdcCameraStep(&camera, 0, 32767, false, 1.0 / 60);
    CHECK(camera.zoom == 0.25);
    KdcCameraStep(&camera, 32767, -32768, true, 1.0 / 60);
    CHECK(!camera.active && camera.yaw == 0 && camera.zoom == 1);
    KdcCameraStep(&camera, 32767, -32768, false, NAN);
    CHECK(!camera.active);

    /* The original framing offset (lead) from the pivot to the native view
     * center is preserved exactly at the original pose... */
    KdcCameraReset(&camera);
    camera.target_x = 3; camera.target_y = 2; camera.target_z = 1;
    camera.lead_x = 1.5; camera.lead_y = -0.25;
    KdcCourseCamera centered = camera;
    centered.target_x += camera.lead_x; centered.target_y += camera.lead_y;
    centered.lead_x = centered.lead_y = 0;
    for (double x = 0; x < 6; x += 1.25) {
        KdcCameraPoint a = KdcCameraProject(&camera, x, 4 - x, 2, 342, 224);
        KdcCameraPoint b = KdcCameraProject(&centered, x, 4 - x, 2, 342, 224);
        CHECK(CLOSE(a.x, b.x) && CLOSE(a.y, b.y));
    }
    /* ...and turns with the course, but is never magnified by zoom: the
     * pivot stays at the same rotated offset from the screen center. */
    camera.yaw = 1.1;
    KdcCameraPoint pivot = KdcCameraProject(&camera, 3, 2, 1, 342, 224);
    camera.zoom = 4;
    p = KdcCameraProject(&camera, 3, 2, 1, 342, 224);
    CHECK(CLOSE(p.x, pivot.x) && CLOSE(p.y, pivot.y));
    CHECK(hypot(p.x - 171, p.y - 112) <= 32 * hypot(1.5, 0.25) + 0.001);

    /* Screen position and depth identify a unique world point; a screen
     * position and height identify one on that plane. */
    camera.zoom = 0.6;
    for (double x = -1; x < 7; x += 1.7) {
        double y = 5 - 0.6 * x, z = 0.3 * x, ux, uy, uz, depth;
        p = KdcCameraProject(&camera, x, y, z, 342, 224);
        CHECK(KdcCameraUnproject(&camera, p.x, p.y, p.depth, 342, 224, &ux, &uy, &uz));
        CHECK(CLOSE(ux, x) && CLOSE(uy, y) && CLOSE(uz, z));
        CHECK(KdcCameraUnprojectPlane(&camera, p.x, p.y, z, 342, 224, &ux, &uy, &depth));
        CHECK(CLOSE(ux, x) && CLOSE(uy, y) && CLOSE(depth, p.depth));
    }
    camera.zoom = 0;
    CHECK(!KdcCameraUnproject(&camera, 1, 1, 1, 342, 224, &p.x, &p.y, &p.depth));
    return 0;
}
