// =============================================================================
//  Lihtne logimine USB Serial kaudu (115200), ajatempli ja mooduli nimega.
//  HWCDC Serial on lõimekindel, seega sobib kasutamiseks kõigist taskidest.
// =============================================================================
#pragma once
#include <Arduino.h>

#define LOG_(lvl, tag, fmt, ...) \
    Serial.printf("[%9lu][%s][%s] " fmt "\r\n", (unsigned long)millis(), lvl, tag, ##__VA_ARGS__)

#define LOGI(tag, fmt, ...) LOG_("I", tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG_("W", tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) LOG_("E", tag, fmt, ##__VA_ARGS__)
