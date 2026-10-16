#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif
#include "resource_channel.h"
#include "resource_protocol.h"
#include "os.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { DEADLINE_MS=5000 };
struct resource_client {
    ClientPtr client;
    int fd, pending, ready, sent;
    int rights[2];
    struct lorie_resource_request request;
    struct lorie_resource_reply reply;
    OsTimerPtr timer;
};
static struct resource_client *resources[MAXCLIENTS];
struct admission { int fd; OsTimerPtr timer; };
static unsigned admissions;
static void close_rights(int fds[2]) {
    for (unsigned i=0;i<2;++i) { if (fds[i]>=0) close(fds[i]); fds[i]=-1; }
}
/* Receive at most two owned descriptors. Every malformed/truncated packet
 * closes installed rights, including descriptors beyond the expected count. */
static int receive(int fd,void *data,size_t size,int fds[2],unsigned *count) {
    union { struct cmsghdr align; char bytes[CMSG_SPACE(2*sizeof(int))]; } control;
    struct iovec iov={data,size};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=&control,.msg_controllen=sizeof(control)};
    ssize_t n=recvmsg(fd,&msg,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
    *count=0; fds[0]=fds[1]=-1;
    if (n<0) return -errno;
    int error=msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC) ? -EPROTO : 0;
    for (struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)) {
        if (c->cmsg_level!=SOL_SOCKET || c->cmsg_type!=SCM_RIGHTS
                || c->cmsg_len<CMSG_LEN(0) || (c->cmsg_len-CMSG_LEN(0))%sizeof(int)) {
            error=-EPROTO; continue;
        }
        size_t bytes=c->cmsg_len-CMSG_LEN(0);
        for (size_t i=0;i<bytes;i+=sizeof(int)) {
            int received; memcpy(&received,(char *)CMSG_DATA(c)+i,sizeof(received));
            if (*count<2) fds[(*count)++]=received;
            else { close(received); error=-EPROTO; }
        }
    }
    if (n!=(ssize_t)size) error=-EPROTO;
    if (error) close_rights(fds);
    return error;
}
static void disconnect(struct resource_client *r) {
    if (r->fd>=0) { RemoveNotifyFd(r->fd); close(r->fd); r->fd=-1; }
}
static void finish(struct resource_client *r,int error) {
    if (r->timer) TimerCancel(r->timer);
    if (r->fd>=0) RemoveNotifyFd(r->fd);
    if (error) {
        disconnect(r); close_rights(r->rights);
        r->reply=(struct lorie_resource_reply){.error=error};
    }
    r->ready=1;
    if (r->pending) AttendClient(r->client);
}
static CARD32 expired(OsTimerPtr timer,CARD32 now,void *data) {
    struct resource_client *r=data;
    finish(r,-ETIMEDOUT); return 0;
}
static void progress(int fd,int events,void *data) {
    struct resource_client *r=data;
    if (!r->sent) {
        ssize_t n=send(fd,&r->request,sizeof(r->request),MSG_DONTWAIT|MSG_NOSIGNAL);
        if (n<0 && (errno==EINTR || errno==EAGAIN)) return;
        if (n!=sizeof(r->request)) { finish(r,-EPIPE); return; }
        r->sent=1;
        if (!SetNotifyFd(fd,progress,X_NOTIFY_READ,r)) { finish(r,-ENOMEM); return; }
    }
    unsigned count=0;
    int error=receive(fd,&r->reply,sizeof(r->reply),r->rights,&count);
    if (error==-EINTR || error==-EAGAIN) return;
    if (!error && (r->reply.error>0 || r->reply.error < -4095 || r->reply.reserved
            || (r->reply.error ? count || r->reply.size : count!=2 || !r->reply.size))) error=-EPROTO;
    finish(r,error);
}
static void client_state(CallbackListPtr *list,void *closure,void *data) {
    ClientPtr client=((NewClientInfoRec *)data)->client;
    if (client->clientState!=ClientStateGone && client->clientState!=ClientStateRetained) return;
    struct resource_client *r=resources[client->index];
    if (!r) return;
    resources[client->index]=NULL;
    disconnect(r); if (r->timer) TimerFree(r->timer);
    close_rights(r->rights); free(r);
}
Bool lorieResourcesInit(void) { return AddCallback(&ClientStateCallback,client_state,NULL); }
Bool lorieResourcesHas(ClientPtr client) { return resources[client->index]!=NULL; }
int lorieResourcesShm(ClientPtr client,uint32_t id,Bool readonly,int fds[2],uint64_t *size) {
    struct resource_client *r=resources[client->index];
    fds[0]=fds[1]=-1; *size=0;
    if (!r) return -ENOTSUP;
    if (r->ready) {
        r->ready=r->pending=0;
        if (r->request.id!=id || r->request.flags!=(readonly ? LORIE_RESOURCE_READONLY : 0)) {
            disconnect(r); close_rights(r->rights); return -EPROTO;
        }
        if (r->reply.error) return r->reply.error;
        fds[0]=r->rights[0]; fds[1]=r->rights[1]; *size=r->reply.size;
        r->rights[0]=r->rights[1]=-1; return 0;
    }
    if (r->fd<0) return -EPIPE;
    if (r->pending) return -EAGAIN;
    r->request=(struct lorie_resource_request){LORIE_RESOURCE_MAGIC,LORIE_RESOURCE_SHM,id,
        readonly ? LORIE_RESOURCE_READONLY : 0};
    r->sent=0;
    /* EVENT_WAIT: channel reply resumes only this X client. Expiry fails the
     * attachment and disables this channel; other clients keep dispatching. */
    r->timer=TimerSet(r->timer,0,DEADLINE_MS,expired,r);
    if (!r->timer || !SetNotifyFd(r->fd,progress,X_NOTIFY_WRITE,r)) {
        disconnect(r); return -ENOMEM;
    }
    r->pending=1; return -EAGAIN;
}
static void discard_admission(struct admission *a) {
    if (a->fd>=0) RemoveNotifyFd(a->fd);
    if (a->timer) TimerFree(a->timer);
    if (a->fd>=0) close(a->fd);
    --admissions; free(a);
}
static CARD32 admission_expired(OsTimerPtr timer,CARD32 now,void *data) {
    struct admission *a=data;
    discard_admission(a); return 0;
}
static void admit(int fd,int events,void *data) {
    struct admission *a=data;
    char hello;
    ssize_t n=recv(fd,&hello,1,MSG_PEEK|MSG_DONTWAIT);
    if (n<0 && (errno==EINTR || errno==EAGAIN)) return;
    if (n!=1) { discard_admission(a); return; }
    struct resource_client *r=NULL;
    if (hello==LORIE_RESOURCE_HELLO) {
        int fds[2]; unsigned count=0;
        int error=receive(fd,&hello,1,fds,&count);
        if (error==-EINTR || error==-EAGAIN) return;
        int type=0; socklen_t length=sizeof(type);
        if (!error && (count!=1 || getsockopt(fds[0],SOL_SOCKET,SO_TYPE,&type,&length)
                || type!=SOCK_SEQPACKET)) error=-EPROTO;
        if (!error && !(r=calloc(1,sizeof(*r)))) error=-ENOMEM;
        if (error) { close_rights(fds); discard_admission(a); return; }
        r->fd=fds[0]; r->rights[0]=r->rights[1]=-1;
    } else if (hello!='l' && hello!='B') { discard_admission(a); return; }
    RemoveNotifyFd(fd); a->fd=-1; discard_admission(a);
    ClientPtr client=AddAuthenticatedClientOnOpenFD(fd);
    if (r) {
        if (!client) { close(r->fd); free(r); }
        else { r->client=client; resources[client->index]=r; }
    }
}
void lorieResourcesAccept(int fd) {
    if (admissions>=MAXCLIENTS) { close(fd); return; }
    struct admission *a=calloc(1,sizeof(*a));
    if (!a) { close(fd); return; }
    a->fd=fd; ++admissions;
    /* EVENT_WAIT: initial X11 byte or resource preface; deadline rejects silent
     * clients without occupying the server thread or bypassing Xauthority. */
    a->timer=TimerSet(NULL,0,DEADLINE_MS,admission_expired,a);
    if (!a->timer || !SetNotifyFd(fd,admit,X_NOTIFY_READ,a)) discard_admission(a);
}
