#include "radio_link.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RADIO_LINK_D2R (3.14159265358979323846 / 180.0)

static const radio_link_station_t radio_link_config_stations[RADIO_LINK_MAX_STATIONS] =
    RADIO_CFG_GROUND_STATIONS;

double radio_link_elevation_deg(const double pos_n[3], const double cwn[3][3],
                                double lat_deg, double lon_deg)
{
    double lat = lat_deg * RADIO_LINK_D2R;
    double lon = lon_deg * RADIO_LINK_D2R;
    double up[3] = {cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat)};
    double sc_w[3];
    double rho[3];
    double range = 0.0;
    double up_dot = 0.0;

    for (int i = 0; i < 3; i++)
    {
        sc_w[i] = cwn[i][0] * pos_n[0] + cwn[i][1] * pos_n[1] + cwn[i][2] * pos_n[2];
        rho[i] = sc_w[i] - RADIO_LINK_EARTH_RADIUS_M * up[i];
        range += rho[i] * rho[i];
        up_dot += rho[i] * up[i];
    }
    range = sqrt(range);
    if (range <= 0.0) return 90.0;

    double s = up_dot / range;
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    return asin(s) / RADIO_LINK_D2R;
}

void radio_link_seed(radio_link_t *link, uint32_t seed)
{
    link->prng = seed ? seed : 1U;
}

static uint32_t radio_link_next(radio_link_t *link)
{
    uint32_t x = link->prng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    link->prng = x;
    return x;
}

static void radio_link_queue_clear(radio_link_queue_t *q)
{
    while (q->count)
    {
        radio_link_packet_t *p = &q->items[q->head];
        free(p->data);
        p->data = NULL;
        q->head = (q->head + 1U) % RADIO_CFG_LINK_QUEUE_DEPTH;
        q->count--;
    }
    q->head = 0;
}

void radio_link_clear_queues(radio_link_t *link)
{
    for (unsigned int d = 0; d < RADIO_LINK_NUM_DIRECTIONS; d++)
        radio_link_queue_clear(&link->queue[d]);
}

void radio_link_reset(radio_link_t *link)
{
    radio_link_clear_queues(link);

    link->mode = (radio_link_mode_t)RADIO_CFG_LINK_MODE;
    link->impairments_enabled = 1;
    link->delay_ns[RADIO_LINK_UP] = (uint64_t)RADIO_CFG_LINK_UPLINK_DELAY_MS * 1000000ULL;
    link->delay_ns[RADIO_LINK_DOWN] = (uint64_t)RADIO_CFG_LINK_DOWNLINK_DELAY_MS * 1000000ULL;
    link->drop_ppm[RADIO_LINK_UP] = RADIO_CFG_LINK_UPLINK_DROP_PPM;
    link->drop_ppm[RADIO_LINK_DOWN] = RADIO_CFG_LINK_DOWNLINK_DROP_PPM;
    radio_link_seed(link, RADIO_CFG_LINK_SEED);

    link->num_stations = RADIO_CFG_NUM_GROUND_STATIONS;
    memcpy(link->stations, radio_link_config_stations, sizeof(link->stations));

    memset(link->dropped, 0, sizeof(link->dropped));
    memset(link->out_of_view, 0, sizeof(link->out_of_view));
    memset(link->overflowed, 0, sizeof(link->overflowed));
    memset(link->corrupt_remaining, 0, sizeof(link->corrupt_remaining));
    memset(link->corrupt_offset, 0, sizeof(link->corrupt_offset));
    memset(link->corrupted, 0, sizeof(link->corrupted));
    memset(link->replayed, 0, sizeof(link->replayed));
    memset(link->last_len, 0, sizeof(link->last_len));

    /* Auto starts down until the first geometry update says otherwise. */
    link->up = 0;
    radio_link_update(link, NULL, NULL);
}

bool radio_link_update(radio_link_t *link, const double pos_n[3], const double cwn[3][3])
{
    uint8_t up = link->up;

    if (link->mode == RADIO_LINK_MODE_ALWAYS)
        up = 1;
    else if (link->mode == RADIO_LINK_MODE_NEVER)
        up = 0;
    else if (pos_n && cwn)
    {
        up = 0;
        for (uint32_t i = 0; i < link->num_stations && !up; i++)
        {
            const radio_link_station_t *s = &link->stations[i];
            if (s->enabled &&
                radio_link_elevation_deg(pos_n, cwn, s->lat_deg, s->lon_deg) >= s->min_elev_deg)
                up = 1;
        }
    }

    bool changed = up != link->up;
    link->up = up;
    return changed;
}

radio_link_admit_t radio_link_admit(radio_link_t *link, unsigned int dir)
{
    if (!link->up)
    {
        link->out_of_view[dir]++;
        return RADIO_LINK_ADMIT_OUT_OF_VIEW;
    }
    if (link->impairments_enabled && link->drop_ppm[dir] > 0U &&
        (radio_link_next(link) % RADIO_LINK_PPM_SCALE) < link->drop_ppm[dir])
    {
        link->dropped[dir]++;
        return RADIO_LINK_ADMIT_DROPPED;
    }
    return RADIO_LINK_ADMIT_OK;
}

uint64_t radio_link_delay_ns(const radio_link_t *link, unsigned int dir)
{
    return link->impairments_enabled ? link->delay_ns[dir] : 0ULL;
}

bool radio_link_enqueue(radio_link_t *link, unsigned int dir, uint64_t release_ns,
                        const uint8_t *data, uint32_t len)
{
    radio_link_queue_t *q = &link->queue[dir];
    if (q->count >= RADIO_CFG_LINK_QUEUE_DEPTH)
    {
        link->overflowed[dir]++;
        return false;
    }

    uint8_t *copy = malloc(len ? len : 1U);
    if (!copy)
    {
        link->overflowed[dir]++;
        return false;
    }
    if (len) memcpy(copy, data, len);

    radio_link_packet_t *p = &q->items[(q->head + q->count) % RADIO_CFG_LINK_QUEUE_DEPTH];
    p->release_ns = release_ns;
    p->len = len;
    p->data = copy;
    q->count++;
    return true;
}

const radio_link_packet_t *radio_link_peek_due(const radio_link_t *link, unsigned int dir,
                                               uint64_t now_ns)
{
    const radio_link_queue_t *q = &link->queue[dir];
    if (!q->count) return NULL;
    const radio_link_packet_t *p = &q->items[q->head];
    return p->release_ns <= now_ns ? p : NULL;
}

void radio_link_pop(radio_link_t *link, unsigned int dir)
{
    radio_link_queue_t *q = &link->queue[dir];
    if (!q->count) return;
    free(q->items[q->head].data);
    q->items[q->head].data = NULL;
    q->head = (q->head + 1U) % RADIO_CFG_LINK_QUEUE_DEPTH;
    q->count--;
}

void radio_link_remember(radio_link_t *link, unsigned int dir, const uint8_t *data, uint32_t len)
{
    if (len == 0U || len > RADIO_LINK_MAX_PACKET) return;
    memcpy(link->last[dir], data, len);
    link->last_len[dir] = len;
}

bool radio_link_corrupt(radio_link_t *link, unsigned int dir, uint8_t *data, uint32_t len)
{
    if (link->corrupt_remaining[dir] == 0U || link->corrupt_offset[dir] >= len) return false;
    data[link->corrupt_offset[dir]] ^= 0x01U;
    link->corrupt_remaining[dir]--;
    link->corrupted[dir]++;
    return true;
}

uint32_t radio_link_replay_copy(radio_link_t *link, unsigned int dir, uint8_t *out)
{
    uint32_t len = link->last_len[dir];
    if (len == 0U) return 0U;
    memcpy(out, link->last[dir], len);
    link->replayed[dir]++;
    return len;
}
