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

KdcCameraPoint KdcCameraProject(const KdcCourseCamera *camera,
                               double x, double y, double z,
                               int width, int height)
{
    x -= camera->target_x;
    y -= camera->target_y;
    z -= camera->target_z;
    double right = 32 * (x + y), forward = 32 * (x - y);
    double c = cos(camera->yaw), s = sin(camera->yaw);
    double horizontal = c * right - s * forward;
    double depth = s * right + c * forward;
    return (KdcCameraPoint){
        width * 0.5 + camera->zoom * horizontal,
        height * 0.5 + camera->zoom * (depth * 0.5 - 16 * z),
        depth + 8 * z,
    };
}
