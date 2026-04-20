#pragma once

// SMesh (Stanford radio club) — Heltec WiFi LoRa 32 V3 + SPI SD carrier.
// Base board pins: variants/esp32s3/heltec_v3/variant.h
//
// We add ontop of the heltec_v3 variant file

#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SPI_MOSI (20)
#define SPI_SCK (21)
#define SPI_MISO (26)
#define SPI_CS (19)
#define SDCARD_CS SPI_CS
