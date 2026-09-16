#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/density_settings.h"

static uint32_t card32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void check(int dpi, int scale, int unscaled) {
    uint8_t bytes[LORIE_DENSITY_SETTINGS_SIZE + 1];
    bytes[LORIE_DENSITY_SETTINGS_SIZE] = 0x7f;
    size_t length = lorieDensitySettings(bytes, dpi, 0x87654321);
    assert(length <= LORIE_DENSITY_SETTINGS_SIZE);
    assert(bytes[LORIE_DENSITY_SETTINGS_SIZE] == 0x7f);
    assert(bytes[0] == 0 && card32(bytes + 4) == 0x87654321 && card32(bytes + 8) == 3);
    const char* names[] = {"Xft/DPI", "Gdk/WindowScalingFactor", "Gdk/UnscaledDPI"};
    uint32_t values[] = {(uint32_t)dpi * 1024, (uint32_t)scale, (uint32_t)unscaled};
    size_t offset = 12;
    for (int i = 0; i < 3; i++) {
        assert(bytes[offset] == 0);
        size_t size = bytes[offset + 2] | (size_t)bytes[offset + 3] << 8;
        assert(size == strlen(names[i]) && !memcmp(bytes + offset + 4, names[i], size));
        offset += 4 + ((size + 3) & ~(size_t)3);
        assert(card32(bytes + offset) == 0x87654321);
        assert(card32(bytes + offset + 4) == values[i]);
        offset += 8;
    }
    assert(offset == length);
}

int main(void) {
    check(24, 1, 24 * 1024);
    check(96, 1, 96 * 1024);
    check(192, 2, 96 * 1024);
    check(312, 3, 104 * 1024);
    check(1536, 16, 96 * 1024);
    uint8_t bytes[LORIE_DENSITY_SETTINGS_SIZE];
    assert(!lorieDensitySettings(bytes, 0, 0));
    assert(!lorieDensitySettings(bytes, 1537, 0));
    puts("X11 density settings verified");
}
