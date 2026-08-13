#pragma once

#include <stdint.h>

enum BuddyMode : uint8_t {
    MODE_UNCHECKED = 0,
    MODE_WORK,
    MODE_MEETING,
    MODE_FREE,
    MODE_BREAK,
};

constexpr int WORK_AM_START = 8 * 60;
constexpr int WORK_AM_END   = 12 * 60;
constexpr int WORK_PM_START = 14 * 60;
constexpr int WORK_PM_END   = 17 * 60 + 30;
constexpr int QUIET_END     = 8 * 60;

inline bool inWorkWindow(int minute) {
    return (minute >= WORK_AM_START && minute < WORK_AM_END) ||
           (minute >= WORK_PM_START && minute < WORK_PM_END);
}

inline bool inQuietHours(int minute) {
    return minute >= 0 && minute < QUIET_END;
}

inline bool monitoringEnabled(BuddyMode mode, int minute) {
    if (mode == MODE_FREE) return minute < 0 || !inQuietHours(minute);
    return mode == MODE_WORK && (minute < 0 || inWorkWindow(minute));
}

inline BuddyMode restoredMode(bool checkedToday, int minute) {
    return checkedToday && minute >= 0 && inWorkWindow(minute)
        ? MODE_WORK : MODE_UNCHECKED;
}

inline bool shouldClearCheckin(int minute) {
    return minute >= WORK_PM_END;
}
