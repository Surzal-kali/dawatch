#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../engine/ringbuffer.h"
#include "../engine/contracts.h"
#include "canary_contact.h"

static uint64_t now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

int main(){
    RingBuffer *rb = rb_create(256, sizeof(ContactReport));
    printf("Canary demo: pushing 3 touches\n");
    CanaryEvent e1 = { .identifier = "honeyfile_001", .action = 1, .actor = "10.0.0.42" };
    CanaryEvent e2 = { .identifier = "honeyfile_001", .action = 1, .actor = "10.0.0.42" };
    CanaryEvent e3 = { .identifier = "honeykey_abc", .action = 0, .actor = "10.0.0.99" };

    ContactReport r;
    r = canary_to_report(now_ms(), &e1);
    rb_push(rb, &r);
    r = canary_to_report(now_ms(), &e2);
    rb_push(rb, &r);
    r = canary_to_report(now_ms(), &e3);
    rb_push(rb, &r);

    printf("Ring buffer count = %zu\n", rb_count(rb));
    ContactReport out;
    while(rb_pop(rb, &out)){
        printf("Contact lane=%d kind=%d ts=%llu payload_len=%d\n",
               out.lane, out.kind, (unsigned long long)out.ts_ms, out.payload_len);
    }
    rb_destroy(rb);
    return 0;
}
