#include "ringbuffer.h"
#include <stdlib.h>
#include <string.h>

RingBuffer* rb_create(size_t capacity, size_t elem_size){
    RingBuffer *rb = calloc(1, sizeof(RingBuffer));
    if(!rb) return NULL;
    rb->capacity = capacity;
    rb->elem_size = elem_size;
    rb->buf = calloc(capacity, elem_size);
    if(!rb->buf){ free(rb); return NULL; }
    rb->head = 0;
    rb->count = 0;
    return rb;
}

void rb_destroy(RingBuffer *rb){
    if(!rb) return;
    free(rb->buf);
    free(rb);
}

bool rb_push(RingBuffer *rb, const void *elem){
    if(!rb || !elem) return false;
    size_t idx = (rb->head + rb->count) % rb->capacity;
    void *dest = (char*)rb->buf + idx * rb->elem_size;
    memcpy(dest, elem, rb->elem_size);
    if(rb->count < rb->capacity){
        rb->count++;
    }else{
        rb->head = (rb->head + 1) % rb->capacity;
    }
    return true;
}

bool rb_pop(RingBuffer *rb, void *out){
    if(!rb || rb->count == 0) return false;
    void *src = (char*)rb->buf + rb->head * rb->elem_size;
    memcpy(out, src, rb->elem_size);
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count--;
    return true;
}

size_t rb_count(const RingBuffer *rb){
    return rb ? rb->count : 0;
}

void rb_reset(RingBuffer *rb){
    if(!rb) return;
    rb->head = 0;
    rb->count = 0;
    memset(rb->buf, 0, rb->capacity * rb->elem_size);
}
