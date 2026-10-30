#ifndef RADIO_LINK_H
#define RADIO_LINK_H

/*
** RF link model for the radio simulator: ground-station visibility, one-way
** transmission delay, random packet loss, and the delay queues. Pure logic
** (no sockets, threads, or locking) so it can be unit tested on its own;
** radio_sim.c serializes all access under its buffer mutex.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_cfg.h"

typedef enum
{
    RADIO_LINK_MODE_AUTO = 0,   /* in view when any enabled station sees the spacecraft */
    RADIO_LINK_MODE_ALWAYS = 1, /* always in view (test override) */
    RADIO_LINK_MODE_NEVER = 2   /* never in view (test override) */
} radio_link_mode_t;

#define RADIO_LINK_UP   0U
#define RADIO_LINK_DOWN 1U
#define RADIO_LINK_NUM_DIRECTIONS 2U

/* Backdoor "direction" selector: UP / DOWN / BOTH. */
#define RADIO_LINK_DIR_BOTH 2U

/*
** Configuration defaults. device_cfg.h normally supplies these (rendered by
** the orchestrator); the fallbacks keep the model compiling and behaving like
** the pre-link-model radio (always in view, no delay, no loss) when it doesn't.
*/
#ifndef RADIO_CFG_LINK_MODE
#define RADIO_CFG_LINK_MODE RADIO_LINK_MODE_ALWAYS
#endif
#ifndef RADIO_CFG_LINK_UPLINK_DELAY_MS
#define RADIO_CFG_LINK_UPLINK_DELAY_MS 0
#endif
#ifndef RADIO_CFG_LINK_DOWNLINK_DELAY_MS
#define RADIO_CFG_LINK_DOWNLINK_DELAY_MS 0
#endif
#ifndef RADIO_CFG_LINK_UPLINK_DROP_PPM
#define RADIO_CFG_LINK_UPLINK_DROP_PPM 0
#endif
#ifndef RADIO_CFG_LINK_DOWNLINK_DROP_PPM
#define RADIO_CFG_LINK_DOWNLINK_DROP_PPM 0
#endif
#ifndef RADIO_CFG_LINK_SEED
#define RADIO_CFG_LINK_SEED 1
#endif
#ifndef RADIO_CFG_LINK_QUEUE_DEPTH
#define RADIO_CFG_LINK_QUEUE_DEPTH 64
#endif
#ifndef RADIO_CFG_NUM_GROUND_STATIONS
#define RADIO_CFG_NUM_GROUND_STATIONS 0
/* Placeholder row so the table is never empty; {lat_deg, lon_deg, min_elev_deg, enabled}. */
#define RADIO_CFG_GROUND_STATIONS { {0.0, 0.0, 0.0, 0} }
#endif

#define RADIO_LINK_MAX_STATIONS \
    ((RADIO_CFG_NUM_GROUND_STATIONS) > 0 ? (RADIO_CFG_NUM_GROUND_STATIONS) : 1)

/* Mean Earth radius used by 42 (World[EARTH].rad), so station positions match. */
#define RADIO_LINK_EARTH_RADIUS_M 6.378145e6

#define RADIO_LINK_PPM_SCALE 1000000U

/* Largest packet the link remembers for replay: a UDP datagram from the ground (the radio reads up to 8192). */
#define RADIO_LINK_MAX_PACKET 8192U

typedef struct
{
    double lat_deg;
    double lon_deg;
    double min_elev_deg;
    uint8_t enabled;
} radio_link_station_t;

typedef struct
{
    uint64_t release_ns;
    uint32_t len;
    uint8_t *data;
} radio_link_packet_t;

/* FIFO of packets waiting out their delay. Release is head-of-line, so order is
 * preserved even if the delay is lowered while packets are in flight. */
typedef struct
{
    radio_link_packet_t items[RADIO_CFG_LINK_QUEUE_DEPTH];
    uint32_t head;
    uint32_t count;
} radio_link_queue_t;

typedef enum
{
    RADIO_LINK_ADMIT_OK = 0,
    RADIO_LINK_ADMIT_OUT_OF_VIEW,
    RADIO_LINK_ADMIT_DROPPED
} radio_link_admit_t;

typedef struct
{
    radio_link_mode_t mode;
    uint8_t impairments_enabled;     /* master switch for delay + loss (not visibility) */
    uint8_t up;                      /* link currently in view */
    uint64_t delay_ns[RADIO_LINK_NUM_DIRECTIONS];
    uint32_t drop_ppm[RADIO_LINK_NUM_DIRECTIONS];
    uint32_t prng;
    uint32_t num_stations;
    radio_link_station_t stations[RADIO_LINK_MAX_STATIONS];
    radio_link_queue_t queue[RADIO_LINK_NUM_DIRECTIONS];

    uint32_t dropped[RADIO_LINK_NUM_DIRECTIONS];      /* lost to random loss */
    uint32_t out_of_view[RADIO_LINK_NUM_DIRECTIONS];  /* lost because link was down */
    uint32_t overflowed[RADIO_LINK_NUM_DIRECTIONS];   /* lost because delay queue was full */

    /* Test-only tampering: damage the next packets, or re-inject the last good one. */
    uint16_t corrupt_remaining[RADIO_LINK_NUM_DIRECTIONS];
    uint16_t corrupt_offset[RADIO_LINK_NUM_DIRECTIONS];
    uint32_t corrupted[RADIO_LINK_NUM_DIRECTIONS];
    uint32_t replayed[RADIO_LINK_NUM_DIRECTIONS];
    uint32_t last_len[RADIO_LINK_NUM_DIRECTIONS];
    uint8_t last[RADIO_LINK_NUM_DIRECTIONS][RADIO_LINK_MAX_PACKET];
} radio_link_t;

/* Elevation (deg) of the spacecraft above a station's local horizon.
 * pos_n is the spacecraft inertial position [m]; cwn is 42's Earth-fixed <-
 * inertial transform. Uses 42's spherical Earth so it agrees with 42's own
 * ground-station positions. */
double radio_link_elevation_deg(const double pos_n[3], const double cwn[3][3],
                                double lat_deg, double lon_deg);

/* Frees queued packets and resets the model to the configured defaults. */
void radio_link_reset(radio_link_t *link);

/* Releases queued packets without touching settings (e.g. radio power cycle). */
void radio_link_clear_queues(radio_link_t *link);

/* Recomputes link->up. ALWAYS/NEVER ignore the geometry; AUTO uses it when
 * pos_n/cwn are non-NULL and otherwise holds the previous value. Returns true
 * if link->up changed. */
bool radio_link_update(radio_link_t *link, const double pos_n[3],
                       const double cwn[3][3]);

/* Seeds the loss PRNG (a zero seed is mapped to a nonzero state). */
void radio_link_seed(radio_link_t *link, uint32_t seed);

/* Decides whether a packet in direction dir (RADIO_LINK_UP/DOWN) gets through
 * right now, updating the loss counters. */
radio_link_admit_t radio_link_admit(radio_link_t *link, unsigned int dir);

/* Remembers the last packet that crossed the link in dir, for radio_link_replay_copy(). */
void radio_link_remember(radio_link_t *link, unsigned int dir, const uint8_t *data, uint32_t len);

/* Flips one bit of data[corrupt_offset] while a corrupt request is pending for dir. Returns true if the packet
 * was damaged. Requests whose offset lies beyond the packet leave it alone and stay pending. */
bool radio_link_corrupt(radio_link_t *link, unsigned int dir, uint8_t *data, uint32_t len);

/* Copies the last packet remembered for dir into out (capacity RADIO_LINK_MAX_PACKET) and counts a replay.
 * Returns its length, or 0 if none has crossed the link yet. */
uint32_t radio_link_replay_copy(radio_link_t *link, unsigned int dir, uint8_t *out);

/* One-way delay currently applied in dir (0 while impairments are disabled). */
uint64_t radio_link_delay_ns(const radio_link_t *link, unsigned int dir);

/* Copies the packet into the dir queue. False (and counted) if full, OOM, or longer than
 * RADIO_LINK_MAX_PACKET. */
bool radio_link_enqueue(radio_link_t *link, unsigned int dir, uint64_t release_ns,
                        const uint8_t *data, uint32_t len);

/* Head packet of dir's queue if it is due at now_ns, else NULL. */
const radio_link_packet_t *radio_link_peek_due(const radio_link_t *link, unsigned int dir,
                                               uint64_t now_ns);

/* Discards the head packet (call after handling radio_link_peek_due()). */
void radio_link_pop(radio_link_t *link, unsigned int dir);

#endif /* RADIO_LINK_H */
