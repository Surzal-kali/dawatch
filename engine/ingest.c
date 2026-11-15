#include "ingest.h"
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

bool ingest_validate(const ContactReport *cr){
    if(!cr) return false;
    if(cr->lane >= LANE_COUNT) return false;
    if(cr->payload_len > sizeof(cr->payload)) return false;
    return true;
}

RouteKind ingest_contact(RingBuffer **lane_buffers, IngestCounters *stats,
                         const ContactReport *cr, bool external){
    // callers validate first; defensively refuse out-of-contract traffic
    if(!lane_buffers || !stats || !cr || cr->lane >= LANE_COUNT)
        return ROUTE_CONTINUE;

    uint8_t lane = cr->lane;
    if(lane_buffers[lane] && rb_push(lane_buffers[lane], cr)){
        stats->ingress[lane]++;
    }else{
        stats->dropped++;
    }

    // A canary touch IS the alert (main README §6): route it on its own lane,
    // outside the fused K-sigma scalar. Only external touches alert —
    // engine-generated liveness is the detector's own tempo.
    if(lane == LANE_CANARY && external){
        stats->canary_alerts++;
        return ROUTE_ALERT;
    }
    return ROUTE_CONTINUE;
}

int ingest_read_contact(int fd, ContactReport *out){
    if(fd < 0 || !out) return -1;
    char *buf = (char *)out;
    size_t got = 0;
    while(got < sizeof(ContactReport)){
        ssize_t n = recv(fd, buf + got, sizeof(ContactReport) - got, 0);
        if(n == 0) return got > 0 ? -1 : 0;
        if(n < 0){
            if(errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
    return 1;
}
