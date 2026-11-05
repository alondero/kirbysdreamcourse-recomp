#include "course_camera.h"
#include <math.h>

static const double pi = 3.14159265358979323846;

void KdcCameraReset(KdcCourseCamera *camera)
{
    *camera = (KdcCourseCamera){.zoom = 1.0};
}

void KdcCameraStep(KdcCourseCamera *camera, int horizontal, int vertical,
                   bool reset, double seconds)
{
    if (reset) { KdcCameraReset(camera); return; }
    if (!isfinite(seconds) || seconds <= 0) return;
    /* Do not jump after a breakpoint or a suspended window. */
    if (seconds > 0.1) seconds = 0.1;
    double x = horizontal / 32768.0, y = vertical / 32768.0;
    double magnitude = sqrt(x * x + y * y);
    const double deadzone = 0.2;
    if (magnitude <= deadzone) return;
    double strength = (fmin(magnitude, 1.0) - deadzone) / (1.0 - deadzone);
    x *= strength / magnitude;
    y *= strength / magnitude;
    camera->active = true;
    camera->yaw = remainder(camera->yaw + x * (pi / 2) * seconds, 2 * pi);
    camera->zoom = fmax(0.25, fmin(4.0, camera->zoom * exp2(-y * seconds)));
}

/* Rotate a horizontal cell offset into screen-aligned (horizontal, depth)
 * pixels of the original projection. */
static void Rotate(const KdcCourseCamera *camera, double x, double y,
                   double *horizontal, double *depth)
{
    double right = 32 * (x + y), forward = 32 * (x - y);
    double c = cos(camera->yaw), s = sin(camera->yaw);
    *horizontal = c * right - s * forward;
    *depth = s * right + c * forward;
}

/* Screen origin of the pivot: the view center, displaced by the rotated
 * but unmagnified original framing offset. */
static void Pivot(const KdcCourseCamera *camera, int width, int height,
                  double *x, double *y)
{
    double horizontal, depth;
    Rotate(camera, camera->lead_x, camera->lead_y, &horizontal, &depth);
    *x = width * 0.5 - horizontal;
    *y = height * 0.5 - depth * 0.5;
}

KdcCameraPoint KdcCameraProject(const KdcCourseCamera *camera,
                               double x, double y, double z,
                               int width, int height)
{
    double horizontal, depth, origin_x, origin_y;
    z -= camera->target_z;
    Rotate(camera, x - camera->target_x, y - camera->target_y, &horizontal, &depth);
    Pivot(camera, width, height, &origin_x, &origin_y);
    return (KdcCameraPoint){
        origin_x + camera->zoom * horizontal,
        origin_y + camera->zoom * (depth * 0.5 - 16 * z),
        depth + 8 * z,
    };
}

static void World(const KdcCourseCamera *camera, double horizontal,
                  double depth, double *x, double *y)
{
    double c = cos(camera->yaw), s = sin(camera->yaw);
    double right = c * horizontal + s * depth;
    double forward = -s * horizontal + c * depth;
    *x = camera->target_x + (right + forward) / 64;
    *y = camera->target_y + (right - forward) / 64;
}

bool KdcCameraUnproject(const KdcCourseCamera *camera, double screen_x,
                        double screen_y, double depth, int width, int height,
                        double *x, double *y, double *z)
{
    if (!(camera->zoom > 0) || !isfinite(depth)) return false;
    double origin_x, origin_y;
    Pivot(camera, width, height, &origin_x, &origin_y);
    double horizontal = (screen_x - origin_x) / camera->zoom;
    double vertical = (screen_y - origin_y) / camera->zoom;
    /* vertical = d/2 - 16z and depth = d + 8z, relative to the target. */
    double relative_z = (depth * 0.5 - vertical) / 20;
    World(camera, horizontal, depth - 8 * relative_z, x, y);
    *z = camera->target_z + relative_z;
    return true;
}

bool KdcCameraUnprojectPlane(const KdcCourseCamera *camera, double screen_x,
                             double screen_y, double z, int width, int height,
                             double *x, double *y, double *depth)
{
    if (!(camera->zoom > 0) || !isfinite(z)) return false;
    double origin_x, origin_y;
    Pivot(camera, width, height, &origin_x, &origin_y);
    double horizontal = (screen_x - origin_x) / camera->zoom;
    double vertical = (screen_y - origin_y) / camera->zoom;
    double relative_z = z - camera->target_z;
    double planar = 2 * (vertical + 16 * relative_z);
    World(camera, horizontal, planar, x, y);
    *depth = planar + 8 * relative_z;
    return true;
}
