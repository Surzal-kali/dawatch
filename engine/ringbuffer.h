#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *buf;
    size_t elem_size;
    size_t capacity;
    size_t head;
    size_t count;
} RingBuffer;

RingBuffer* rb_create(size_t capacity, size_t elem_size);
void rb_destroy(RingBuffer *rb);
bool rb_push(RingBuffer *rb, const void *elem);
bool rb_pop(RingBuffer *rb, void *out);
size_t rb_count(const RingBuffer *rb);
void rb_reset(RingBuffer *rb);
