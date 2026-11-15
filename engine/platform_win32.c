/* platform_win32.c — winsock implementation of the platform shim.
 * Build: MSYS2 UCRT64 (mingw-w64 gcc, afunix.h ships in headers >= 10.0),
 * link ws2_32. Behavior notes live in platform.h's header comment. */
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <windows.h>

static bool g_wsa_ready;

void bluec_platform_init(void){
    if(g_wsa_ready) return;
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return; /* socket() will fail loudly */
    g_wsa_ready = true;
}
static void ensure_ready(void){ if(!g_wsa_ready) bluec_platform_init(); }

const char *bluec_socket_path(void){
    static char buf[108];
    const char *env = getenv("BLUEC_SOCKET_PATH");
    if(env && env[0] && strlen(env) < sizeof(buf)){
        strcpy(buf, env);
        return buf;
    }
    /* %TEMP% is set for user sessions; under SYSTEM it is C:\Windows\Temp. */
    const char *tmp = getenv("TEMP");
    if(!tmp || !tmp[0] || strlen(tmp) + 16 >= sizeof(buf)) tmp = "C:\\Windows\\Temp";
    snprintf(buf, sizeof(buf), "%s\\bluec.socket", tmp);
    return buf;
}

bluec_fd_t bluec_bind_listen(const char *socket_path){
    ensure_ready();
    /* Windows AF_UNIX keeps the bind address materialized as a file; the
     * AF_UNIX-on-Windows design note says DeleteFile before rebind. */
    DeleteFileA(socket_path);
    SOCKET fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == INVALID_SOCKET) return BLUEC_FD_INVALID;
    SOCKADDR_UN addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
       listen(fd, 8) != 0){
        closesocket(fd);
        return BLUEC_FD_INVALID;
    }
    return (bluec_fd_t)fd;
}

bluec_fd_t bluec_connect(const char *socket_path){
    ensure_ready();
    SOCKET fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == INVALID_SOCKET) return BLUEC_FD_INVALID;
    SOCKADDR_UN addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0){
        closesocket(fd);
        return BLUEC_FD_INVALID;
    }
    return (bluec_fd_t)fd;
}

bluec_fd_t bluec_accept(bluec_fd_t srv){
    ensure_ready();
    SOCKET cfd = accept((SOCKET)srv, NULL, NULL);
    if(cfd == INVALID_SOCKET) return BLUEC_FD_INVALID;
    /* POSIX-restore: accepted sockets inherit the listener's non-blocking
     * mode on Windows; our readers expect blocking whole-report reads. */
    u_long mode = 0;
    ioctlsocket(cfd, FIONBIO, &mode);
    return (bluec_fd_t)cfd;
}

void bluec_close(bluec_fd_t fd){
    if(fd != BLUEC_FD_INVALID) closesocket((SOCKET)fd);
}

int bluec_set_nonblocking(bluec_fd_t fd, bool on){
    u_long mode = on ? 1 : 0;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode) == 0 ? 0 : -1;
}

int bluec_write(bluec_fd_t fd, const void *buf, size_t n){
    ensure_ready();
    if(n > INT_MAX) return -1; /* caller payloads are <= 144B, guarded anyway */
    return send((SOCKET)fd, (const char*)buf, (int)n, 0);
}

int bluec_recv(bluec_fd_t fd, void *buf, size_t n){
    ensure_ready();
    if(n > INT_MAX) return -1;
    return recv((SOCKET)fd, (char*)buf, (int)n, 0);
}

int bluec_test_stream_pair(bluec_fd_t fds[2]){
    SOCKET listener = INVALID_SOCKET;
    SOCKET peer = INVALID_SOCKET;
    SOCKET accepted = INVALID_SOCKET;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);

    ensure_ready();
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listener == INVALID_SOCKET) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if(bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
       listen(listener, 1) != 0 ||
       getsockname(listener, (struct sockaddr*)&addr, &addr_len) != 0){
        closesocket(listener);
        return -1;
    }

    peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(peer == INVALID_SOCKET ||
       connect(peer, (struct sockaddr*)&addr, sizeof(addr)) != 0){
        if(peer != INVALID_SOCKET) closesocket(peer);
        closesocket(listener);
        return -1;
    }
    accepted = accept(listener, NULL, NULL);
    closesocket(listener);
    if(accepted == INVALID_SOCKET){
        closesocket(peer);
        return -1;
    }

    fds[0] = (bluec_fd_t)peer;
    fds[1] = (bluec_fd_t)accepted;
    return 0;
}

bool bluec_would_block(void){
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}

bool bluec_retry(void){
    return WSAGetLastError() == WSAEINTR;
}

void bluec_unlink(const char *path){
    ensure_ready();
    if(!DeleteFileA(path)){
        DWORD e = GetLastError();
        (void)e; /* missing file is the expected steady state before first bind */
    }
}

uint64_t bluec_now_ms(void){
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (uint64_t)(t.QuadPart * 1000ULL / f.QuadPart);
}

void bluec_sleep_ms(uint32_t ms){
    Sleep(ms);
}
