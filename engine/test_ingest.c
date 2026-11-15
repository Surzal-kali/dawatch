#include "ingest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Ingest conformance (main README §11 + engine.README matrix): lifetime
 * counters bumped in the push path (never derived from rb_count), malformed
 * rejection, canary split routing, full-report framing on a stream socket. */

#define CAP 64

static ContactReport make_cr(uint8_t lane, uint8_t kind){
    ContactReport cr = {0};
    cr.lane = lane;
    cr.kind = kind;
    cr.ts_ms = 42;
    return cr;
}

static void test_canary_split_routing(void){
    ContactReport wire   = make_cr(LANE_WIRE, 1);
    ContactReport canary = make_cr(LANE_CANARY, 2);
    RingBuffer *bufs[LANE_COUNT] = {0};
    bufs[LANE_WIRE] = rb_create(CAP, sizeof(ContactReport));
    bufs[LANE_CANARY] = rb_create(CAP, sizeof(ContactReport));
    IngestCounters st = {0};

    // canary split: external canary touch alerts; wire never does; the
    // engine-generated (internal) canary liveness never alerts
    assert(ingest_contact(bufs, &st, &canary, true) == ROUTE_ALERT);
    assert(ingest_contact(bufs, &st, &wire, true) == ROUTE_CONTINUE);
    assert(ingest_contact(bufs, &st, &canary, false) == ROUTE_CONTINUE);

    assert(st.canary_alerts == 1);                        // only the external touch
    assert(st.ingress[LANE_CANARY] == 2);                 // all contacts counted
    assert(st.ingress[LANE_WIRE] == 1);
    assert(rb_count(bufs[LANE_CANARY]) == 2);

    rb_destroy(bufs[LANE_WIRE]); rb_destroy(bufs[LANE_CANARY]);
}

/* Matrix acceptance: counter test at ring saturation (push capacity x 10,
 * deltas stay true) — the lifetime counter carries the truth while the ring
 * occupancy saturates and wraps. */
static void test_counters_true_at_saturation(void){
    RingBuffer *bufs[LANE_COUNT] = {0};
    bufs[LANE_WIRE] = rb_create(CAP, sizeof(ContactReport));
    IngestCounters st = {0};

    for(int i = 0; i < CAP * 10; i++){
        ContactReport cr = make_cr(LANE_WIRE, 1);
        assert(ingest_contact(bufs, &st, &cr, true) == ROUTE_CONTINUE);
    }
    assert(st.ingress[LANE_WIRE] == (uint64_t)CAP * 10);  // deltas stay true
    assert(rb_count(bufs[LANE_WIRE]) == CAP);             // ring wrapped instead
    assert(st.dropped == 0);
    rb_destroy(bufs[LANE_WIRE]);
}

static void test_push_failure_counted_as_dropped(void){
    RingBuffer *bufs[LANE_COUNT] = {0};                   // wire lane has buffer
    bufs[LANE_WIRE] = rb_create(8, sizeof(ContactReport));
    IngestCounters st = {0};

    ContactReport ep = make_cr(LANE_ENDPOINT, 1);
    assert(ingest_contact(bufs, &st, &ep, true) == ROUTE_CONTINUE);
    assert(st.dropped == 1 && st.ingress[LANE_ENDPOINT] == 0);

    // NULL report is a caller bug, not a lost contact: refused, not counted
    assert(ingest_contact(bufs, &st, NULL, true) == ROUTE_CONTINUE);
    assert(st.dropped == 1);
    rb_destroy(bufs[LANE_WIRE]);
}

static void test_validate(void){
    ContactReport ok      = make_cr(LANE_API, 3);
    assert(ingest_validate(&ok));

    ContactReport bad_lane = ok; bad_lane.payload_len = 0; bad_lane.lane = 7;
    assert(!ingest_validate(&bad_lane));
    ContactReport bad_len  = ok; bad_len.payload_len = 129;
    assert(!ingest_validate(&bad_len));
    ContactReport max_len  = ok; max_len.payload_len = 128;
    assert(ingest_validate(&max_len));                    // bound, not under it
    assert(!ingest_validate(NULL));
}

static void test_read_full_report_framing(void){
    ContactReport sent = make_cr(LANE_API, 3);
    sent.ts_ms = 12345;
    sent.payload_len = 4;
    ContactReport out;
    bluec_fd_t fds[2];

    // full report in one write
    assert(bluec_test_stream_pair(fds) == 0);
    assert(bluec_write(fds[0], &sent, sizeof(sent)) == (int)sizeof(sent));
    bluec_close(fds[0]);
    memset(&out, 0xAA, sizeof(out));
    assert(ingest_read_contact(fds[1], &out) == 1);
    assert(out.lane == LANE_API && out.kind == 3 && out.ts_ms == 12345);

    // clean EOF before any byte
    bluec_close(fds[1]);
    assert(bluec_test_stream_pair(fds) == 0);
    bluec_close(fds[0]);
    assert(ingest_read_contact(fds[1], &out) == 0);

    // partial report then EOF -> malformed (never treated as a report)
    bluec_close(fds[1]);
    assert(bluec_test_stream_pair(fds) == 0);
    ContactReport part = {0};
    assert(bluec_write(fds[0], &part, 100) == 100);
    bluec_close(fds[0]);
    assert(ingest_read_contact(fds[1], &out) == -1);

    // multiple reports drained in order on one connection
    bluec_close(fds[1]);
    assert(bluec_test_stream_pair(fds) == 0);
    assert(bluec_write(fds[0], &sent, sizeof(sent)) == (int)sizeof(sent));
    assert(bluec_write(fds[0], &sent, sizeof(sent)) == (int)sizeof(sent));
    bluec_close(fds[0]);
    assert(ingest_read_contact(fds[1], &out) == 1);
    assert(ingest_read_contact(fds[1], &out) == 1);
    assert(ingest_read_contact(fds[1], &out) == 0);
    bluec_close(fds[1]);

    // bad args
    assert(ingest_read_contact(-1, &out) == -1);
}

int main(void){
    test_canary_split_routing();
    test_counters_true_at_saturation();
    test_push_failure_counted_as_dropped();
    test_validate();
    test_read_full_report_framing();
    printf("test_ingest: PASS\n");
    return 0;
}
