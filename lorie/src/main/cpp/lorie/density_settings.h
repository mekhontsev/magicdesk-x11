#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LORIE_DENSITY_SETTINGS_SIZE 112

static inline void densityCard32(uint8_t* bytes, uint32_t value) {
    for (int i = 0; i < 4; i++) bytes[i] = value >> (i * 8);
}

/* XSETTINGS format 8, explicitly little endian, with all three values in one update. */
static inline size_t lorieDensitySettings(uint8_t bytes[LORIE_DENSITY_SETTINGS_SIZE], int dpi, uint32_t serial) {
    if (dpi < 24 || dpi > 1536) return 0;
    const char* names[] = {"Xft/DPI", "Gdk/WindowScalingFactor", "Gdk/UnscaledDPI"};
    int scale = (dpi + 48) / 96;
    if (scale < 1) scale = 1;
    uint32_t values[] = {(uint32_t)dpi * 1024, (uint32_t)scale, (uint32_t)(dpi * 1024 + scale / 2) / scale};
    memset(bytes, 0, LORIE_DENSITY_SETTINGS_SIZE);
    densityCard32(bytes + 4, serial);
    densityCard32(bytes + 8, 3);
    size_t offset = 12;
    for (int i = 0; i < 3; i++) {
        size_t length = strlen(names[i]);
        bytes[offset + 2] = length;
        memcpy(bytes + offset + 4, names[i], length);
        offset += 4 + ((length + 3) & ~(size_t)3);
        densityCard32(bytes + offset, serial);
        densityCard32(bytes + offset + 4, values[i]);
        offset += 8;
    }
    return offset;
}
