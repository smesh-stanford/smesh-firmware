#pragma once

// SMesh (Stanford radio club) — Heltec WiFi LoRa 32 V3 + SPI SD (Adafruit-style wiring).
// Base board: variants/esp32s3/heltec_v3/variant.h
//
// SPI (host naming): SCK=19, MISO=34 (card DO), MOSI=26 (card DI), CS=47.
// GPIO 33 is GPIO_WIND_COUNTER on heltec_v3/variant.h — use 34 for SD MISO, not 33.
// CD (card detect) on GPIO48 is optional hardware; Arduino SD.begin() does not use it unless you add code.

#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SPI_SCK (19)
#define SPI_MISO (34)
#define SPI_MOSI (26)
#define SPI_CS (47)
#define SDCARD_CS SPI_CS

/** Card detect (optional); not read by FSCommon / SD library today. */
#define SMESH_SD_CD_GPIO (48)
