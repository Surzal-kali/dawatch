#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../engine/contracts.h"
#include "../engine/platform.h"

static uint64_t now_ms(void){
    return bluec_now_ms();
}

int main(void){
    bluec_platform_init();
    bluec_fd_t fd = bluec_connect(bluec_socket_path());
    if(fd == BLUEC_FD_INVALID){
        fprintf(stderr, "canary_client: engine socket not reachable at %s\n", bluec_socket_path());
        return 1;
    }
    // send a canary contact
    ContactReport cr = {0};
    cr.ts_ms = now_ms();
    cr.lane = 4;
    cr.kind = 1;
    int written = snprintf((char*)cr.payload, sizeof(cr.payload), "honey_%u", rand()%1000);
    if (written < 0) {
        cr.payload_len = 0;
    } else if ((size_t)written >= sizeof(cr.payload)) {
        cr.payload_len = sizeof(cr.payload);
    } else {
        cr.payload_len = (uint16_t)written;
    }
    if(bluec_write(fd, &cr, sizeof(cr)) != (int)sizeof(cr)){
        fprintf(stderr, "canary_client: short/blocked send\n");
        bluec_close(fd);
        return 1;
    }
    printf("Sent canary contact\n");
    bluec_close(fd);
    return 0;
}
