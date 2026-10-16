#ifndef LORIE_RESOURCE_PROTOCOL_H
#define LORIE_RESOURCE_PROTOCOL_H
#include <stdint.h>

/* Optional pre-X11 admission byte with one SCM_RIGHTS SOCK_SEQPACKET endpoint.
 * Ordinary X11 byte-order markers are untouched. The endpoint is a capability
 * scoped by its issuer; requests never contain a store path or credentials. */
#define LORIE_RESOURCE_HELLO 'R'
#define LORIE_RESOURCE_MAGIC UINT32_C(0x4d445231)
#define LORIE_RESOURCE_SHM 1U
#define LORIE_RESOURCE_READONLY 1U
struct lorie_resource_request { uint32_t magic, operation, id, flags; };
/* Success carries backing FD then lifetime-lease FD. Error carries no FDs and
 * a negative Linux errno. Retain the lease until the last mapping reference. */
struct lorie_resource_reply { int32_t error; uint32_t reserved; uint64_t size; };
#endif
