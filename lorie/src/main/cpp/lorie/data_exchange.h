#pragma once
#include <stdint.h>
#include "embedded.h"
struct _Window;

/* Embedded content protocol. Payload descriptors contain bytes, never pointers or paths.
 * CLIPBOARD and XDND share selection conversion, but have independent ownership. */
#define LORIE_DATA_MAX_BYTES (128U * 1024U * 1024U)
#define LORIE_DATA_MAX_TARGETS 64
#define LORIE_DATA_CHUNK 65536

typedef struct {
    unsigned char t, operation, channel, hasFd;
    uint32_t serial, offer, output, window;
    int32_t x, y;
    char mime[128];
} LorieDataEvent;

#ifdef __cplusplus
extern "C" {
#endif
void lorieDataInit(void);
void lorieDataReset(void);
void lorieDataShutdown(void);
void lorieDataCommand(const LorieDataEvent* event, int fd);
void lorieSendDataEvent(const LorieDataEvent* event, int fd);
void lorieDataPointer(uint32_t output, uint32_t window, int button, Bool down);
Bool lorieOutputPoint(uint32_t output, uint32_t window, int x, int y,
        struct _Window** selected, int* rootX, int* rootY);
#ifdef __cplusplus
}
#endif
