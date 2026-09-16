#pragma once
#include <android/hardware_buffer.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct DmaCopyContext DmaCopyContext;
typedef struct DmaCopySource DmaCopySource;
typedef struct DmaCopyDestination DmaCopyDestination;
typedef enum {
    DMA_COPY_DECLINED,
    DMA_COPY_COMPLETE,
    DMA_COPY_UNCONFIRMED
} DmaCopyResult;

// Optional, synchronous EXA accelerator. No Vulkan objects escape this boundary.
DmaCopyContext* dmaCopyCreate(void);
void dmaCopyRelease(DmaCopyContext* context);
DmaCopySource* dmaCopyImportSource(DmaCopyContext* context, int fd, off_t offset, int width, int height,
                                   int strideBytes, const void* pixels);
DmaCopyDestination* dmaCopyImportDestination(DmaCopyContext* context, AHardwareBuffer* buffer);
void dmaCopyReleaseSource(DmaCopySource* source);
void dmaCopyReleaseDestination(DmaCopyDestination* destination);
bool dmaCopyReady(DmaCopyContext* context);
DmaCopyResult dmaCopyRect(DmaCopySource* source, DmaCopyDestination* destination, int srcX, int srcY,
                         int dstX, int dstY, int width, int height);
uint64_t dmaCopyTakeStagedBytes(DmaCopyContext* context);
