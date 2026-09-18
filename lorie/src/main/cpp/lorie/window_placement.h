#pragma once

/* Keep a transient reachable without resizing toolkit-owned content. Oversized
 * content extends the output canvas; the existing aspect-fit renderer scales it. */
static inline int loriePlaceTransientAxis(int position, int extent, int origin, int available) {
    int last = origin + (extent < available ? available - extent : 0);
    return position < origin ? origin : position > last ? last : position;
}

static inline int lorieOutputAxis(int normalized, int origin, int extent) {
    int value = normalized < 0 ? 0 : normalized > 10000 ? 10000 : normalized;
    return origin + (int)((long long)value * (extent > 0 ? extent - 1 : 0) / 10000);
}
