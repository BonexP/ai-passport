// main/app_clock.c —— 时间窗口判断与 UTC 格式化的实现。契约见 app_clock.h。
#include "app_clock.h"

#include <stdio.h>

bool app_clock_epoch_is_plausible(int64_t epoch_seconds) {
    return epoch_seconds >= APP_CLOCK_VALID_FROM_EPOCH && epoch_seconds < APP_CLOCK_VALID_UNTIL_EPOCH;
}

// epoch 天数 → 公历年月日（Howard Hinnant 的 civil_from_days）。
// 负数天（1970 年之前）也要正确：公式里的 era 分支就是为此而写。
static void civil_from_days(int64_t days, int *year, unsigned *month, unsigned *day) {
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;

    *year = y + (m <= 2 ? 1 : 0);
    *month = m;
    *day = d;
}

bool app_clock_format_utc(int32_t epoch, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (epoch <= 0) {
        return false;
    }

    const int64_t days = epoch / 86400;
    const int64_t seconds = epoch % 86400;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, &year, &month, &day);

    const int written = snprintf(buf, cap, "%04d-%02u-%02u %02u:%02uZ", year, month, day,
                                 (unsigned)(seconds / 3600), (unsigned)((seconds % 3600) / 60));
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}
