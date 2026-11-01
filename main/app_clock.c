// main/app_clock.c —— 时间窗口判断的实现。契约见 app_clock.h。
#include "app_clock.h"

bool app_clock_epoch_is_plausible(int64_t epoch_seconds) {
    return epoch_seconds >= APP_CLOCK_VALID_FROM_EPOCH && epoch_seconds < APP_CLOCK_VALID_UNTIL_EPOCH;
}
