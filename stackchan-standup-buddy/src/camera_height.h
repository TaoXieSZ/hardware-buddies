// Intermittent on-board camera probe for pitch (height) adjustment.
//
// The GC0308's SCCB shares G11/G12 with the internal I2C bus (touch / LED
// expander / IMU / RTC), so the camera cannot stay on. Instead we borrow the
// bus for a ~0.7s window every CAM_PROBE_MS: release I2C → init camera →
// grab two frames 300ms apart → frame-diff for motion → deinit → reclaim I2C.
// No face model: motion centroid only, which is all pitch adjustment needs.
#pragma once
#include <stdint.h>

struct CamProbe {
    bool  ok;      // camera cycle completed
    bool  motion;  // frame diff above threshold
    float cy;      // vertical centroid of motion, 0..1 (0 = top of frame)
};

CamProbe cameraProbeOnce();
