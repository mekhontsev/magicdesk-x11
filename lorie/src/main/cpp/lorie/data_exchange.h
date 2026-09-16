#pragma once
#include <stdint.h>
struct _Window;

/* Embedded content protocol. Payload descriptors contain bytes, never pointers or paths.
 * CLIPBOARD and XDND share selection conversion, but have independent ownership. */
enum {
    LORIE_DATA_ENABLE = 1, LORIE_DATA_OFFER, LORIE_DATA_READ, LORIE_DATA_REQUEST,
    LORIE_DATA_REPLY, LORIE_DATA_ENTER, LORIE_DATA_MOVE, LORIE_DATA_LEAVE,
    LORIE_DATA_DROP, LORIE_DATA_STATUS, LORIE_DATA_FINISH, LORIE_DATA_CANCEL, LORIE_DATA_BEGIN
};
enum { LORIE_DATA_CLIPBOARD, LORIE_DATA_DRAG };
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
