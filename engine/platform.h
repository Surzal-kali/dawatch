#ifndef BLUEC_PLATFORM_H
#define BLUEC_PLATFORM_H
/*
 * platform.h — thin portability shim over the engine's OS-sensitive surface.
 *
 * Rationale (win32 port): ingest is same-host AF_UNIX (contracts.h). Windows
 * implements AF_UNIX natively (Win10 17063+; winsock SOCKADDR_UN via
 * <afunix.h>), so the same-host AF_UNIX transport contract holds on both
 * platforms — no TCP fallback anywhere. This shim owns every OS-sensitive
 * call: sockets, unlink, monotonic clock, sleep, non-blocking mode, and the
 * error-shape predicates. Lane and engine code never include winsock or
 * POSIX socket headers directly.
 *
 * Error handling: POSIX errno and winsock error spaces differ. NEVER branch
 * on bare `errno` for socket calls — use bluec_would_block() and
 * bluec_retry(), which resolve the right error space per platform.
 *
 * Windows accept() semantics differ from POSIX: accepted sockets inherit the
 * listener's non-blocking mode, so a non-blocking listener would surface
 * WSAEWOULDBLOCK as a spurious "malformed/short stream" mid-drain
 * (ingest_read_contact expects blocking whole-report reads).
 * bluec_accept() restores POSIX semantics by clearing non-blocking mode on
 * every accepted fd.
 *
 * On Linux this shim must compile to behavior identical to the pre-shim
 * tree — `make all && make test && make smoke` unchanged.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#  include <winsock2.h>   /* must precede any transitive windows.h */
#  include <ws2tcpip.h>
#  include <afunix.h>
typedef intptr_t bluec_fd_t;      /* holds a SOCKET safely */
#else
#  include <sys/socket.h>
#  include <sys/un.h>
typedef int bluec_fd_t;
#endif

#define BLUEC_FD_INVALID ((bluec_fd_t)-1)

/* ---- lifecycle ----
 * Winsock requires WSAStartup before winsock calls (WSAEINTR/perror traps
 * aside, socket() itself fails first). Win32: idempotent WSAStartup. POSIX:
 * no-op. Call once at the top of main(); shim entry points defensively
 * ensure it. */
void bluec_platform_init(void);

/* ---- socket path ----
 * Env override via BLUEC_SOCKET_PATH, else platform default:
 *   POSIX  "/tmp/bluec.socket"  (the pre-shim tree value)
 *   win32  "%TEMP%\bluec.socket" (C:\Windows\Temp fallback, SYSTEM-safe)
 * Path-based AF_UNIX only — Windows has no abstract namespace. */
const char *bluec_socket_path(void);

/* ---- socket primitives ---- */
bluec_fd_t bluec_bind_listen(const char *socket_path); /* unlink+socket+bind+listen(8) */
bluec_fd_t bluec_connect(const char *socket_path);     /* blocking connect */
bluec_fd_t bluec_accept(bluec_fd_t srv);               /* + POSIX fd semantics */
void       bluec_close(bluec_fd_t fd);
int        bluec_set_nonblocking(bluec_fd_t fd, bool on); /* 0 ok, -1 fail */
int        bluec_write(bluec_fd_t fd, const void *buf, size_t n);
int        bluec_recv(bluec_fd_t fd, void *buf, size_t n);

/* ---- error predicates (use these INSTEAD of bare errno on sockets) ---- */
bool bluec_would_block(void); /* EAGAIN/EWOULDBLOCK | WSAEWOULDBLOCK/EINPROGRESS */
bool bluec_retry(void);       /* EINTR | WSAEINTR */

/* ---- fs / time / sleep ---- */
void     bluec_unlink(const char *path); /* missing file is OK (best-effort) */
uint64_t bluec_now_ms(void);             /* CLOCK_MONOTONIC | QPC */
void     bluec_sleep_ms(uint32_t ms);
#endif /* BLUEC_PLATFORM_H */
