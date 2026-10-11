// Prints crab rectangles from include/muse_crab.h for the keyframe preview, so the preview draws exactly
// what the firmware draws. Input lines on stdin: "<act> <t_ms> <now_ms> <level> <look_deg>".
// Output: one line per frame, "x,y,w,h,e;x,y,w,h,e;..." (e = 1 for eye pixels).
//   c++ -std=c++17 -Iinclude tools/muse-crab/dump_frames.cpp -o /tmp/dump_frames

#include "muse_crab.h"

#include <cstdio>

int main()
{
    int act = 0;
    unsigned t = 0;
    unsigned now = 0;
    int level = 0;
    float look = 0;
    std::vector<stopwatch::CrabRect> rects;
    while (std::scanf("%d %u %u %d %f", &act, &t, &now, &level, &look) == 5) {
        stopwatch::crabRects(stopwatch::crabPose(static_cast<stopwatch::CrabAct>(act), t, now, level, look), rects);
        for (const auto& r : rects) std::printf("%d,%d,%d,%d,%d;", r.x, r.y, r.w, r.h, r.eye ? 1 : 0);
        std::printf("\n");
    }
    return 0;
}
