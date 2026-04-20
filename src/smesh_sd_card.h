#pragma once

// SMesh Heltec V3 + SD: smoke test and optional mirror of LOG_* lines to SD.
#if defined(ARCH_ESP32) && defined(HAS_SDCARD) && defined(SMESH_HELTEC_V3_SD) && !defined(SDCARD_USE_SOFT_SPI)

void smesh_sd_init_log_session(void);
void smesh_sd_run_smoke_test(void);
void smesh_sd_append_log_line(const char *line);

#else

static inline void smesh_sd_init_log_session(void) {}
static inline void smesh_sd_run_smoke_test(void) {}
static inline void smesh_sd_append_log_line(const char *) {}

#endif
