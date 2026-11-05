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
    return 0;
}
