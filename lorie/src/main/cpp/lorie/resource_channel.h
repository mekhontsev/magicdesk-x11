#ifndef LORIE_RESOURCE_CHANNEL_H
#define LORIE_RESOURCE_CHANNEL_H
#include <stdint.h>
#include "dixstruct.h"
#ifdef __cplusplus
extern "C" {
#endif
Bool lorieResourcesInit(void);
void lorieResourcesAccept(int fd);
Bool lorieResourcesHas(ClientPtr client);
/* -EAGAIN suspends the current request until readiness/deadline; success
 * transfers backing and lease descriptors. All calls run on the X thread. */
int lorieResourcesShm(ClientPtr client,uint32_t id,Bool readonly,int fds[2],uint64_t *size);
#ifdef __cplusplus
}
#endif
#endif
