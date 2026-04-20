#include "configuration.h"

#if defined(ARCH_ESP32) && defined(HAS_SDCARD) && defined(SMESH_HELTEC_V3_SD) && !defined(SDCARD_USE_SOFT_SPI)

#include "FSCommon.h"
#include "RTC.h"
#include "meshUtils.h"
#include <SD.h>
#include <cstdio>
#include <ctime>

// Full path to the serial mirror log for this boot (set by smesh_sd_init_log_session).
static char smesh_sd_log_file[128];

void smesh_sd_init_log_session(void)
{
    smesh_sd_log_file[0] = '\0';
    if (SD.cardType() == CARD_NONE)
        return;

    char session[48];
    uint32_t t = getValidTime(RTCQualityDevice, false);
    // If RTC/GPS/phone has not set quality yet, t is 0 — use a per-boot folder name instead.
    static constexpr uint32_t MIN_SYNC_EPOCH = 1704067200UL; // 2024-01-01 00:00 UTC
    if (t >= MIN_SYNC_EPOCH) {
        struct tm tm;
        time_t tt = (time_t)t;
        gmtime_r(&tt, &tm);
        snprintf(session, sizeof(session), "%04u-%02u-%02u_%02u-%02u-%02u", (unsigned)(tm.tm_year + 1900),
                 (unsigned)(tm.tm_mon + 1), (unsigned)tm.tm_mday, (unsigned)tm.tm_hour, (unsigned)tm.tm_min,
                 (unsigned)tm.tm_sec);
    } else {
        snprintf(session, sizeof(session), "boot_%08lx", (unsigned long)millis());
    }

    snprintf(smesh_sd_log_file, sizeof(smesh_sd_log_file), "/logs/%s/smesh_serial.log", session);
    LOG_INFO("smesh SD log session: %s", smesh_sd_log_file);
}

void smesh_sd_run_smoke_test(void)
{
    char buf[64];
    if (!sdCardSmokeTest("/smesh_sd_smoke.txt", buf, sizeof(buf))) {
        LOG_WARN("smesh SD smoke: failed (no card or SD I/O error)");
        return;
    }
    LOG_INFO("smesh SD smoke read back: %s", buf);
}

void smesh_sd_append_log_line(const char *line)
{
    if (!smesh_sd_log_file[0])
        return;
    (void)sdCardAppendLine(smesh_sd_log_file, line, true);
}

#endif
