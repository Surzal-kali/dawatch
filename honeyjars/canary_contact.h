#pragma once
#include "../engine/contracts.h"

typedef struct {
    char identifier[64];
    uint8_t action; // 0=open,1=read,2=exec
    char actor[64];
} CanaryEvent;

static inline ContactReport canary_to_report(uint64_t ts_ms, const CanaryEvent *e){
    ContactReport r = {0};
    r.ts_ms = ts_ms;
    r.lane = 4; // canary
    r.kind = e->action;
    // pack identifier+actor into payload, simple
    size_t off = 0;
    memcpy(r.payload + off, e->identifier, 64); off+=64;
    memcpy(r.payload + off, e->actor, 64); off+=64;
    r.payload_len = (uint16_t)off;
    return r;
}
