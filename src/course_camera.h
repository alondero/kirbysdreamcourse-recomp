#ifndef KDC_COURSE_CAMERA_H
#define KDC_COURSE_CAMERA_H

#include <stdbool.h>

/* Presentation coordinates: x/y are course cells, z is upward elevation
 * (one elevation step is 16 pixels in the original view). No guest writes. */
typedef struct KdcCourseCamera {
    double yaw, zoom;
    double target_x, target_y, target_z;
    bool active;
} KdcCourseCamera;

typedef struct KdcCameraPoint { double x, y, depth; } KdcCameraPoint;

void KdcCameraReset(KdcCourseCamera *camera);
/* Axes are signed SDL units. Up (negative Y) zooms in. Call once per
 * simulated frame, never from a presenter that may run more often. */
void KdcCameraStep(KdcCourseCamera *camera, int horizontal, int vertical,
                   bool reset, double seconds);
KdcCameraPoint KdcCameraProject(const KdcCourseCamera *camera,
                               double x, double y, double z,
                               int width, int height);

#endif
