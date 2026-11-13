#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "../engine/contracts.h"

#define SOCKET_PATH "/tmp/bluec.socket"

static uint64_t now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

int main(int argc, char **argv){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0){ perror("socket"); return 1; }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("connect");
        return 1;
    }
    // send a canary contact
    ContactReport cr = {0};
    cr.ts_ms = now_ms();
    cr.lane = 4;
    cr.kind = 1;
    snprintf((char*)cr.payload, 64, "honey_%u", rand()%1000);
    cr.payload_len = 64;
    write(fd, &cr, sizeof(cr));
    printf("Sent canary contact\n");
    close(fd);
    return 0;
}
