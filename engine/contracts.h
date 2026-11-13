#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BLUEC_CONTRACT_VERSION 1

/* ---------- Core boundary objects ---------- */

typedef struct {
    uint64_t window_start_ms;
    uint64_t window_end_ms;
    // Per-lane footprint vectors, compact
    // Wire
    double wire_tempo_mean;
    double wire_tempo_std;
    uint32_t wire_flow_count;
    // Endpoint
    uint32_t ep_process_events;
    uint32_t ep_unique_cmdlines;
    // Auth
    uint32_t auth_logons;
    uint32_t auth_failures;
    // API
    uint32_t api_calls;
    uint32_t api_config_changes;
    // Canary
    uint32_t canary_touches;
    // Cross-lane correlation features
    double cross_corr_score;
} WindowFeatures;

typedef struct {
    float probability;   // calibrated 0-1
    uint8_t class_id;    // 0 = benign, >0 = threat class
    uint8_t reserved[3];
} Score;

typedef struct {
    char target_service[64];
    char version_range[32];
    uint16_t cwe_class;
    uint8_t primitive;   // enum
    char cve[32];
    uint8_t delivery_vector; // enum
    uint8_t reserved[3];
} PoCDescriptor;

typedef enum {
    ROUTE_CONTINUE = 0,
    ROUTE_BURST = 1,
    ROUTE_ALERT = 2,
    ROUTE_DECOY = 3
} RouteKind;

typedef struct {
    RouteKind kind;
    uint32_t params; // lane mask / priority
    uint64_t ttl_ms;
} Route;

/* ---------- Lane contact reports ---------- */

typedef struct {
    uint64_t ts_ms;
    uint8_t lane; // 0 wire,1 ep,2 auth,3 api,4 canary
    uint8_t kind;
    uint8_t reserved[2];
    // compact payload, max 128 bytes
    uint8_t payload[128];
    uint16_t payload_len;
} ContactReport;

typedef struct {
    uint64_t request_id;
    uint64_t window_start_ms;
    uint64_t window_end_ms;
    uint8_t lane_mask;
} BurstRequest;

/* ---------- Controller config ---------- */

typedef struct {
    uint64_t baseline_interval_ms;
    double poisson_lambda_per_min; // for random sampling
    double k_sigma_event;
    uint32_t ringbuf_capacity; // per lane
} EngineConfig;
