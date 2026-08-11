#include "../src/mode_logic.h"
#include <cassert>

int main() {
    assert(!inWorkWindow(7 * 60 + 59));
    assert(inWorkWindow(8 * 60));
    assert(!inWorkWindow(12 * 60));
    assert(inWorkWindow(14 * 60));
    assert(!inWorkWindow(17 * 60 + 30));

    assert(!monitoringEnabled(MODE_UNCHECKED, 9 * 60));
    assert(monitoringEnabled(MODE_WORK, 9 * 60));
    assert(!monitoringEnabled(MODE_WORK, 13 * 60));
    assert(monitoringEnabled(MODE_FREE, 20 * 60));
    assert(!monitoringEnabled(MODE_FREE, 2 * 60));
    assert(!monitoringEnabled(MODE_MEETING, 9 * 60));
    assert(!monitoringEnabled(MODE_BREAK, 9 * 60));
    assert(inQuietHours(0));
    assert(inQuietHours(7 * 60 + 59));
    assert(!inQuietHours(8 * 60));
    assert(monitoringEnabled(MODE_FREE, -1));

    assert(restoredMode(true, 9 * 60) == MODE_WORK);
    assert(restoredMode(true, 13 * 60) == MODE_UNCHECKED);
    assert(restoredMode(false, 9 * 60) == MODE_UNCHECKED);
    assert(shouldClearCheckin(17 * 60 + 30));
    return 0;
}
