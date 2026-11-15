#define _POSIX_C_SOURCE 200809L

/* platform_posix.c — Linux passthrough behavior identical to the pre-shim
 * tree. Every function is one or two lines of the code it replaces. */
#include "platform.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

static bool g_posix_ready;

void bluec_platform_init(void){
    g_posix_ready = true; /* no-op on POSIX; kept for symmetry */
}
static void ensure_ready(void){ if(!g_posix_ready) g_posix_ready = true; }

const char *bluec_socket_path(void){
    static char buf[108];
    const char *env = getenv("BLUEC_SOCKET_PATH");
    if(env && strlen(env) > 0 && strlen(env) < sizeof(buf)){
        snprintf(buf, sizeof(buf), "%s", env);
        return buf;
    }
    return "/tmp/bluec.socket";
}

bluec_fd_t bluec_bind_listen(const char *socket_path){
    ensure_ready();
    unlink(socket_path); /* stale listener cleanup; missing file fine */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) return BLUEC_FD_INVALID;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){ close(fd); return BLUEC_FD_INVALID; }
    if(listen(fd, 8) < 0){ close(fd); return BLUEC_FD_INVALID; }
    return fd;
}

bluec_fd_t bluec_connect(const char *socket_path){
    ensure_ready();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) return BLUEC_FD_INVALID;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        close(fd);
        return BLUEC_FD_INVALID;
    }
    return fd;
}

bluec_fd_t bluec_accept(bluec_fd_t srv){
    ensure_ready();
    int cfd = accept(srv, NULL, NULL);
    if(cfd < 0) return BLUEC_FD_INVALID;
    /* POSIX accept returns blocking fds; enforce it (no-op on fresh fds). */
    int flags = fcntl(cfd, F_GETFL, 0);
    if(flags >= 0) fcntl(cfd, F_SETFL, flags & ~O_NONBLOCK);
    return cfd;
}

void bluec_close(bluec_fd_t fd){
    if(fd != BLUEC_FD_INVALID) close(fd);
}

int bluec_set_nonblocking(bluec_fd_t fd, bool on){
    ensure_ready();
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) return -1;
    if(on) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) < 0 ? -1 : 0;
}

int bluec_write(bluec_fd_t fd, const void *buf, size_t n){
    ensure_ready();
    return (int)write(fd, buf, n);
}

int bluec_recv(bluec_fd_t fd, void *buf, size_t n){
    ensure_ready();
    return (int)recv(fd, buf, n, 0);
}

int bluec_test_stream_pair(bluec_fd_t fds[2]){
    ensure_ready();
    return socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

bool bluec_would_block(void){
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool bluec_retry(void){
    return errno == EINTR;
}

void bluec_unlink(const char *path){
    ensure_ready();
    unlink(path); /* missing file: errno set, silently ignored */
}

uint64_t bluec_now_ms(void){
    ensure_ready();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void bluec_sleep_ms(uint32_t ms){
    struct timespec req = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&req, NULL);
}
