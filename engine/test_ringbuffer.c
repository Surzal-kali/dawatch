#include "ringbuffer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "contracts.h"
#include "platform.h"

/* Ring-buffer conformance (main README §11): eviction at saturation, wrap
 * correctness, bounded memory — capacity stays capacity, O(window). */

static void test_wrap_and_eviction(void){
    RingBuffer *rb = rb_create(4, sizeof(int));
    assert(rb && rb->capacity == 4);
    for(int i = 0; i < 6; i++) assert(rb_push(rb, &i));   // overflow capacity
    assert(rb_count(rb) == 4);                            // oldest evicted
    int v;
    assert(rb_pop(rb, &v) && v == 2);                     // 0,1 evicted; order
    assert(rb_pop(rb, &v) && v == 3);                     // survives wrap
    assert(rb_pop(rb, &v) && v == 4);
    assert(rb_pop(rb, &v) && v == 5);
    assert(!rb_pop(rb, &v));                              // empty stays empty
    rb_destroy(rb);
}

static void test_saturation_bounded_memory(void){
    RingBuffer *rb = rb_create(64, sizeof(ContactReport));
    for(int i = 0; i < 64 * 10; i++){                     // capacity x 10
        ContactReport cr = {0};
        cr.ts_ms = (uint64_t)i;
        assert(rb_push(rb, &cr));
    }
    assert(rb_count(rb) == 64);                           // count stays capacity
    ContactReport out;
    uint64_t expect = 64 * 10 - 64;                       // newest contiguous window
    while(rb_pop(rb, &out)){
        assert(out.ts_ms == expect);
        expect++;
    }
    assert(expect == 64 * 10);
    rb_reset(rb);
    assert(rb_count(rb) == 0 && rb->head == 0);
    rb_destroy(rb);
}

static void test_null_safety(void){
    int v = 1;
    assert(rb_count(NULL) == 0);
    assert(!rb_push(NULL, &v));
    assert(!rb_pop(NULL, &v));
    rb_destroy(NULL);                                     // no crash
    RingBuffer *rb = rb_create(2, sizeof(v));
    assert(!rb_push(rb, NULL));
    rb_destroy(rb);
}

/* Keep the linker honest on the socket include set used by real emitters. */
static void test_socket_include_compiles(void){
    bluec_fd_t fds[2];
    assert(bluec_test_stream_pair(fds) == 0);
    bluec_close(fds[0]); bluec_close(fds[1]);
}

int main(void){
    test_wrap_and_eviction();
    test_saturation_bounded_memory();
    test_null_safety();
    test_socket_include_compiles();
    printf("test_ringbuffer: PASS\n");
    return 0;
}
