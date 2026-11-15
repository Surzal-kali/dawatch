#pragma once
#include "../engine/contracts.h"

/*
 * Wire lane — flow-feature shape and contact emission.
 *
 * The wire lane never sees discrete events; it sees tempo. Traffic is reduced
 * to flow features (timing, volume, direction split, port/protocol class,
 * tokenized peer identity) and the lane monitor computes per-window statistics
 * over them. Nothing here retains raw packets or payload bytes — the ring
 * buffers and contacts carry features only (main README §6/§8; enforced by
 * negative tests, not comments).
 *
 * Endpoint identities: peer tokens are consistent same-shape pseudonyms
 * (FNV-1a over the real id, keyed by a salt). Behavioral correlations survive
 * (same real peer -> same token); real ids must never enter buffers, contacts,
 * or logs. Production callers MUST pass a deployment-secret salt — the
 * unsalted hash of an address space is reversible by dictionary. Zero salt is
 * acceptable only in demos/tests whose "real" ids are already fiction.
 *
 * Payload packing (per contracts.h conventions): fixed-width fields,
 * largest-first offsets, host (little-endian) byte order — same-host AF_UNIX
 * transport only. payload_len is set to bytes actually packed.
 *
 * WindowFeatures mapping (minimum wire metadata for the engine footprint):
 *   wire_tempo_mean / wire_tempo_std  <- FLOW_TICK payload tempo fields
 *   wire_flow_count                   <- FLOW_TICK flow_count
 * Port/protocol classes and peer tokens refine per-peer detection; they ride
 * BEACON_SUSPECT / NEW_PEER contacts.
 */

BLUEC_STATIC_ASSERT(sizeof(ContactReport) == 144, wire_flow_needs_contract);
BLUEC_STATIC_ASSERT(sizeof(((ContactReport *)0)->payload) == 128, wire_flow_needs_payload);

/* WireFlow is lane-local state (never shipped raw across the socket); only
 * ContactReport payloads cross. Fixed-width, largest-first, 48 bytes. */
typedef struct {
    uint64_t flow_start_ms;      // first packet seen for this flow
    uint64_t flow_last_ms;       // most recent activity
    uint64_t bytes_up;           // host -> peer
    uint64_t bytes_down;         // peer -> host
    uint64_t peer_token;         // tokenized identity; 0 = untracked peer
    uint32_t pkt_count;
    uint16_t dst_port;
    uint8_t  protocol_class;
    uint8_t  reserved;
} WireFlow;

BLUEC_STATIC_ASSERT(sizeof(WireFlow) == 48, wire_flow_size);

typedef enum {
    WIRE_PROTO_OTHER = 0,
    WIRE_PROTO_TCP   = 1,
    WIRE_PROTO_UDP   = 2,
    WIRE_PROTO_ICMP  = 3
} WireProtocolClass;

/* Contact kinds on lane 0 (ContactReport.kind). */
typedef enum {
    WIRE_KIND_FLOW_TICK     = 1,  // per-window tempo/volume aggregate
    WIRE_KIND_BEACON_SUSPECT = 2, // one peer: regular inter-arrival cadence
    WIRE_KIND_NEW_PEER      = 3   // first sighting within bounded peer memory
} WireContactKind;

/* Consistent same-shape pseudonym for a real peer id. Same (id, salt) pair
 * always yields the same token. */
uint64_t wire_peer_token(const char *real_id, uint64_t salt);

/* ---- FLOW_TICK (40 bytes) ---- */
typedef struct {
    double tempo_mean_ms;        // mean inter-flow-arrival within the window
    double tempo_std_ms;
    uint32_t flow_count;
    uint64_t bytes_up;
    uint64_t bytes_down;         // + 4 bytes padding
} WireTickStats;

/* ---- BEACON_SUSPECT (36 bytes) ---- */
typedef struct {
    uint64_t peer_token;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint32_t interval_mean_ms;   // mean inter-arrival of the flow series
    uint32_t jitter_ppm;         // std/mean of intervals, parts per million
    uint16_t occurrences;        // intervals observed for this episode
} WireBeaconStats;               // + 2 bytes padding

/* ---- NEW_PEER (20 bytes) ---- */
typedef struct {
    uint64_t peer_token;
    uint64_t first_seen_ms;
    uint16_t dst_port;
    uint8_t  protocol_class;
} WirePeerStats;                 // + 1 byte padding

void wire_pack_flow_tick(ContactReport *r, uint64_t ts_ms, const WireTickStats *st);
void wire_pack_beacon(ContactReport *r, uint64_t ts_ms, const WireBeaconStats *st);
void wire_pack_new_peer(ContactReport *r, uint64_t ts_ms, const WirePeerStats *st);

void wire_unpack_flow_tick(const ContactReport *r, WireTickStats *st);
void wire_unpack_beacon(const ContactReport *r, WireBeaconStats *st);
void wire_unpack_new_peer(const ContactReport *r, WirePeerStats *st);
