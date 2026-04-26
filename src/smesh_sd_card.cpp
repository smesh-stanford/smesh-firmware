#include "configuration.h"

#if defined(ARCH_ESP32) && defined(HAS_SDCARD) && defined(SMESH_HELTEC_V3_SD) && !defined(SDCARD_USE_SOFT_SPI)

#include "FSCommon.h"
#include "RTC.h"
#include "meshUtils.h"
#include <SD.h>
#include <cstdio>
#include <ctime>
#include <cstring>

// Full paths for this boot session (set by smesh_sd_init_log_session).
static char smesh_sd_system_log_file[128];
static char smesh_sd_warn_error_file[128];
static char smesh_sd_telemetry_env_file[128];
static char smesh_sd_telemetry_air_file[128];
static char smesh_sd_telemetry_power_file[128];
static char smesh_sd_telemetry_device_file[128];
static uint32_t smesh_sd_warn_error_failed_writes = 0;
static uint32_t smesh_sd_warn_error_last_failure_report_ms = 0;

static void smeshSdFormatTimestamp(uint32_t epochSec, char *out, size_t outBytes)
{
    if (!out || outBytes == 0)
        return;

    static constexpr uint32_t MIN_SYNC_EPOCH = 1704067200UL; // 2024-01-01 00:00 UTC
    if (epochSec >= MIN_SYNC_EPOCH) {
        struct tm tmValue;
        time_t timeValue = (time_t)epochSec;
        gmtime_r(&timeValue, &tmValue);
        snprintf(out, outBytes, "%04u-%02u-%02u %02u:%02u:%02u", (unsigned)(tmValue.tm_year + 1900),
                 (unsigned)(tmValue.tm_mon + 1), (unsigned)tmValue.tm_mday, (unsigned)tmValue.tm_hour,
                 (unsigned)tmValue.tm_min, (unsigned)tmValue.tm_sec);
    } else {
        snprintf(out, outBytes, "boot_%lu", (unsigned long)millis());
    }
}

struct SmeshSdTelemetryCsvSchema {
    pb_size_t variantTag;
    const char *path;
    const char *header;
};

static const SmeshSdTelemetryCsvSchema *smeshSdTelemetryCsvSchema(const meshtastic_Telemetry *telemetry)
{
    if (!telemetry)
        return nullptr;

    const SmeshSdTelemetryCsvSchema schemas[] = {
        {meshtastic_Telemetry_environment_metrics_tag, smesh_sd_telemetry_env_file,
         "datetime,fromNode,temperature,relativeHumidity,barometricPressure,gasResistance,iaq,windDirection,windSpeed,"
         "rxSnr,rxRssi,rxTime,hopStart,hopLimit"},
        {meshtastic_Telemetry_air_quality_metrics_tag, smesh_sd_telemetry_air_file,
         "datetime,fromNode,pm10Standard,pm25Standard,pm100Standard,pm10Environmental,pm25Environmental,pm100Environmental,"
         "rxSnr,rxRssi,rxTime,hopStart,hopLimit"},
        {meshtastic_Telemetry_power_metrics_tag, smesh_sd_telemetry_power_file,
         "datetime,fromNode,ch3Voltage,ch3Current,rxSnr,rxRssi,rxTime,hopStart,hopLimit"},
        {meshtastic_Telemetry_device_metrics_tag, smesh_sd_telemetry_device_file,
         "datetime,fromNode,batteryLevel,voltage,channelUtilization,airUtilTx,rxSnr,rxRssi,rxTime,hopStart,hopLimit"},
    };

    for (const auto &schema : schemas) {
        if (schema.variantTag == telemetry->which_variant)
            return &schema;
    }
    return nullptr;
}

static void smeshSdOptionalFloat(char *out, size_t outBytes, bool hasValue, float value)
{
    if (!out || outBytes == 0)
        return;
    if (!hasValue) {
        out[0] = '\0';
        return;
    }
    snprintf(out, outBytes, "%.3f", value);
}

static void smeshSdOptionalUnsigned(char *out, size_t outBytes, bool hasValue, unsigned long value)
{
    if (!out || outBytes == 0)
        return;
    if (!hasValue) {
        out[0] = '\0';
        return;
    }
    snprintf(out, outBytes, "%lu", value);
}

static void smeshSdFormatTelemetryCsvRow(const meshtastic_MeshPacket *packet, const meshtastic_Telemetry *telemetry, char *out,
                                         size_t outBytes)
{
    if (!packet || !telemetry || !out || outBytes == 0)
        return;

    char timestamp[32];
    smeshSdFormatTimestamp(packet->rx_time, timestamp, sizeof(timestamp));
    unsigned long fromNode = (unsigned long)packet->from;

    switch (telemetry->which_variant) {
    case meshtastic_Telemetry_environment_metrics_tag: {
        const auto &m = telemetry->variant.environment_metrics;
        char temperature[16], humidity[16], barometricPressure[16], gasResistance[16], iaq[12], windDirection[12], windSpeed[16];
        smeshSdOptionalFloat(temperature, sizeof(temperature), m.has_temperature, m.temperature);
        smeshSdOptionalFloat(humidity, sizeof(humidity), m.has_relative_humidity, m.relative_humidity);
        smeshSdOptionalFloat(barometricPressure, sizeof(barometricPressure), m.has_barometric_pressure, m.barometric_pressure);
        smeshSdOptionalFloat(gasResistance, sizeof(gasResistance), m.has_gas_resistance, m.gas_resistance);
        smeshSdOptionalUnsigned(iaq, sizeof(iaq), m.has_iaq, (unsigned long)m.iaq);
        smeshSdOptionalUnsigned(windDirection, sizeof(windDirection), m.has_wind_direction, (unsigned long)m.wind_direction);
        smeshSdOptionalFloat(windSpeed, sizeof(windSpeed), m.has_wind_speed, m.wind_speed);
        snprintf(out, outBytes,
                 "%s,0x%08lx,%s,%s,%s,%s,%s,%s,%s,%.2f,%ld,%lu,%u,%u", timestamp, fromNode, temperature, humidity,
                 barometricPressure, gasResistance, iaq, windDirection, windSpeed, packet->rx_snr, (long)packet->rx_rssi,
                 (unsigned long)packet->rx_time, (unsigned)packet->hop_start, (unsigned)packet->hop_limit);
        break;
    }
    case meshtastic_Telemetry_air_quality_metrics_tag: {
        const auto &m = telemetry->variant.air_quality_metrics;
        char pm10Standard[12], pm25Standard[12], pm100Standard[12], pm10Env[12], pm25Env[12], pm100Env[12];
        smeshSdOptionalUnsigned(pm10Standard, sizeof(pm10Standard), m.has_pm10_standard, (unsigned long)m.pm10_standard);
        smeshSdOptionalUnsigned(pm25Standard, sizeof(pm25Standard), m.has_pm25_standard, (unsigned long)m.pm25_standard);
        smeshSdOptionalUnsigned(pm100Standard, sizeof(pm100Standard), m.has_pm100_standard, (unsigned long)m.pm100_standard);
        smeshSdOptionalUnsigned(pm10Env, sizeof(pm10Env), m.has_pm10_environmental, (unsigned long)m.pm10_environmental);
        smeshSdOptionalUnsigned(pm25Env, sizeof(pm25Env), m.has_pm25_environmental, (unsigned long)m.pm25_environmental);
        smeshSdOptionalUnsigned(pm100Env, sizeof(pm100Env), m.has_pm100_environmental, (unsigned long)m.pm100_environmental);
        snprintf(out, outBytes,
                 "%s,0x%08lx,%s,%s,%s,%s,%s,%s,%.2f,%ld,%lu,%u,%u", timestamp, fromNode, pm10Standard, pm25Standard,
                 pm100Standard, pm10Env, pm25Env, pm100Env, packet->rx_snr, (long)packet->rx_rssi,
                 (unsigned long)packet->rx_time, (unsigned)packet->hop_start, (unsigned)packet->hop_limit);
        break;
    }
    case meshtastic_Telemetry_power_metrics_tag: {
        const auto &m = telemetry->variant.power_metrics;
        char ch3Voltage[16], ch3Current[16];
        smeshSdOptionalFloat(ch3Voltage, sizeof(ch3Voltage), m.has_ch3_voltage, m.ch3_voltage);
        smeshSdOptionalFloat(ch3Current, sizeof(ch3Current), m.has_ch3_current, m.ch3_current);
        snprintf(out, outBytes, "%s,0x%08lx,%s,%s,%.2f,%ld,%lu,%u,%u", timestamp, fromNode, ch3Voltage, ch3Current,
                 packet->rx_snr, (long)packet->rx_rssi,
                 (unsigned long)packet->rx_time, (unsigned)packet->hop_start, (unsigned)packet->hop_limit);
        break;
    }
    case meshtastic_Telemetry_device_metrics_tag: {
        const auto &m = telemetry->variant.device_metrics;
        char batteryLevel[12], voltage[16], channelUtilization[16], airUtilTx[16];
        smeshSdOptionalUnsigned(batteryLevel, sizeof(batteryLevel), m.has_battery_level, (unsigned long)m.battery_level);
        smeshSdOptionalFloat(voltage, sizeof(voltage), m.has_voltage, m.voltage);
        smeshSdOptionalFloat(channelUtilization, sizeof(channelUtilization), m.has_channel_utilization, m.channel_utilization);
        smeshSdOptionalFloat(airUtilTx, sizeof(airUtilTx), m.has_air_util_tx, m.air_util_tx);
        snprintf(out, outBytes, "%s,0x%08lx,%s,%s,%s,%s,%.2f,%ld,%lu,%u,%u", timestamp, fromNode, batteryLevel, voltage,
                 channelUtilization, airUtilTx, packet->rx_snr, (long)packet->rx_rssi,
                 (unsigned long)packet->rx_time, (unsigned)packet->hop_start, (unsigned)packet->hop_limit);
        break;
    }
    default:
        out[0] = '\0';
    }
}

void smesh_sd_init_log_session(void)
{
    smesh_sd_system_log_file[0] = '\0';
    smesh_sd_warn_error_file[0] = '\0';
    smesh_sd_telemetry_env_file[0] = '\0';
    smesh_sd_telemetry_air_file[0] = '\0';
    smesh_sd_telemetry_power_file[0] = '\0';
    smesh_sd_telemetry_device_file[0] = '\0';
    if (SD.cardType() == CARD_NONE)
        return;

    // a human readable identifier for this where it's based on the time
    // if time isn't available it'll be NOT named after the current time but instead
    // "boot" with the millis timestamp
    //
    // 48 as a number that's hopefully enough for most log sessions
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
        // otherwise, time must not be available since the date would otherwise 
        // be some obnoxiously early date set in the past
        snprintf(session, sizeof(session), "boot_%08lx", (unsigned long)millis());
    }

    snprintf(smesh_sd_system_log_file, sizeof(smesh_sd_system_log_file), "/logs/%s/system.log", session);
    snprintf(smesh_sd_warn_error_file, sizeof(smesh_sd_warn_error_file), "/logs/%s/warn_error.log", session);
    snprintf(smesh_sd_telemetry_env_file, sizeof(smesh_sd_telemetry_env_file), "/logs/%s/telemetry_environment.csv", session);
    snprintf(smesh_sd_telemetry_air_file, sizeof(smesh_sd_telemetry_air_file), "/logs/%s/telemetry_airquality.csv", session);
    snprintf(smesh_sd_telemetry_power_file, sizeof(smesh_sd_telemetry_power_file), "/logs/%s/telemetry_power.csv", session);
    snprintf(smesh_sd_telemetry_device_file, sizeof(smesh_sd_telemetry_device_file), "/logs/%s/telemetry_device.csv", session);
    LOG_INFO("smesh SD log session: %s", smesh_sd_system_log_file);
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
    if (!smesh_sd_system_log_file[0])
        return;
    (void)sdCardAppendLine(smesh_sd_system_log_file, line, true);
}

void smesh_sd_append_warn_error_line(const char *logLevel, const char *line)
{
    if (!smesh_sd_warn_error_file[0] || !line || !line[0])
        return;

    char timestamp[32];
    smeshSdFormatTimestamp(getValidTime(RTCQualityDevice, false), timestamp, sizeof(timestamp));

    char formattedLine[480];
    snprintf(formattedLine, sizeof(formattedLine), "%s [%s] %s", timestamp, logLevel ? logLevel : "UNKNOWN", line);

    if (!sdCardAppendLineCritical(smesh_sd_warn_error_file, formattedLine)) {
        smesh_sd_warn_error_failed_writes++;
        uint32_t nowMs = millis();
        if (nowMs - smesh_sd_warn_error_last_failure_report_ms > 15000U) {
            smesh_sd_warn_error_last_failure_report_ms = nowMs;
            printf("WARN | SD warn/error append failures=%lu\n", (unsigned long)smesh_sd_warn_error_failed_writes);
        }
    }
}

void smesh_sd_log_received_telemetry(const meshtastic_MeshPacket *packet, const meshtastic_Telemetry *telemetry)
{
    if (!packet || !telemetry)
        return;

    const SmeshSdTelemetryCsvSchema *csvSchema = smeshSdTelemetryCsvSchema(telemetry);
    if (!csvSchema || !csvSchema->path || !csvSchema->header || !csvSchema->path[0])
        return;

    char row[320];
    smeshSdFormatTelemetryCsvRow(packet, telemetry, row, sizeof(row));
    if (!row[0])
        return;

    (void)sdCardAppendCsvRow(csvSchema->path, csvSchema->header, row, true);
}

#endif
