#ifndef KDC_COURSE_CAMERA_H
#define KDC_COURSE_CAMERA_H

#include <stdbool.h>

/* Presentation coordinates: x/y are course cells, z is upward elevation
 * (one elevation step is 16 pixels in the original view). No guest writes.
 * The camera orbits and zooms about target (the player's ground point).
 * lead_x/lead_y is the original framing offset from target to the native
 * view center. It turns with the course but is not magnified by zoom, so
 * the player remains within that original distance of the screen center. */
typedef struct KdcCourseCamera {
    double yaw, zoom;
    double target_x, target_y, target_z;
    double lead_x, lead_y;
    bool active;
} KdcCourseCamera;

typedef struct KdcCameraPoint { double x, y, depth; } KdcCameraPoint;

void KdcCameraReset(KdcCourseCamera *camera);
/* Axes are signed SDL units. Up (negative Y) zooms in. Call once per
 * simulated frame, never from a presenter that may run more often. */
void KdcCameraStep(KdcCourseCamera *camera, int horizontal, int vertical,
                   bool reset, double seconds);
/* Larger depth is nearer the viewer. Depth does not depend on zoom. */
KdcCameraPoint KdcCameraProject(const KdcCourseCamera *camera,
                               double x, double y, double z,
                               int width, int height);
/* Inverses of KdcCameraProject: from a screen position and its depth, or
 * from a screen position on the horizontal plane at height z. */
bool KdcCameraUnproject(const KdcCourseCamera *camera, double screen_x,
                        double screen_y, double depth, int width, int height,
                        double *x, double *y, double *z);
bool KdcCameraUnprojectPlane(const KdcCourseCamera *camera, double screen_x,
                             double screen_y, double z, int width, int height,
                             double *x, double *y, double *depth);

#endif
