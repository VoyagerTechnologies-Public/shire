/*
 * Unity tests for the radio RF link model (comp/radio/sim/radio_link.c):
 * visibility geometry, loss PRNG, and the delay queues.
 *
 * The model is compiled into this test with a fixed configuration so the tests
 * do not depend on whatever device_cfg.h the orchestrator last generated.
 */
#define RADIO_CFG 1 /* skip device_cfg.h; every setting radio_link.h needs is defined below */
#define RADIO_CFG_LINK_MODE RADIO_LINK_MODE_AUTO
#define RADIO_CFG_LINK_UPLINK_DELAY_MS 250
#define RADIO_CFG_LINK_DOWNLINK_DELAY_MS 500
#define RADIO_CFG_LINK_UPLINK_DROP_PPM 1000
#define RADIO_CFG_LINK_DOWNLINK_DROP_PPM 2000
#define RADIO_CFG_LINK_SEED 12345U
#define RADIO_CFG_LINK_QUEUE_DEPTH 4
#define RADIO_CFG_NUM_GROUND_STATIONS 3
/* {lat, lon, min_elev, enabled}: sub-satellite point, 90 E, and a masked-high station */
#define RADIO_CFG_GROUND_STATIONS { \
    { 0.0, 0.0, 5.0, 1 }, \
    { 0.0, 90.0, 5.0, 1 }, \
    { 0.0, 10.0, 60.0, 1 }, \
}

#include <stdlib.h>

#include "unity.h"

/* Let a test make the model's packet allocation fail. */
static int g_fail_malloc;
static void *test_malloc(size_t size) { return g_fail_malloc ? NULL : malloc(size); }
#define malloc test_malloc

#include "radio_link.c"

#undef malloc

#define ALTITUDE_M 500000.0

/* Unity is built without double support, so compare angles by hand. */
#define ASSERT_NEAR(expected, actual, tol) \
    TEST_ASSERT_TRUE_MESSAGE(fabs((expected) - (actual)) <= (tol), "angle outside tolerance")

static const double IDENTITY[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
/* 180 degree rotation about Earth's polar axis */
static const double FLIP_Z[3][3] = {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}};
static const double OVER_LON_0[3] = {RADIO_LINK_EARTH_RADIUS_M + ALTITUDE_M, 0, 0};

static radio_link_t g_link;

void setUp(void) { g_fail_malloc = 0; radio_link_reset(&g_link); }
void tearDown(void) { radio_link_clear_queues(&g_link); }

/* ------------------------------------------------------------------------- */
static void test_elevation_zenith_is_90_degrees(void)
{
    ASSERT_NEAR(90.0, radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, 0.0), 1e-6);
}

static void test_elevation_far_side_is_negative(void)
{
    TEST_ASSERT_TRUE(radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, 180.0) < 0.0);
    TEST_ASSERT_TRUE(radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, 90.0) < 0.0);
}

static void test_elevation_decreases_with_distance_from_subpoint(void)
{
    double near = radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, 5.0);
    double mid = radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, 15.0);
    double far = radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, 30.0);
    TEST_ASSERT_TRUE(near > mid);
    TEST_ASSERT_TRUE(mid > 0.0);
    TEST_ASSERT_TRUE(far < 0.0);
}

static void test_elevation_horizon_matches_geometry(void)
{
    /* Horizon sits at central angle acos(R / (R + h)); elevation crosses zero there. */
    double r = RADIO_LINK_EARTH_RADIUS_M;
    double horizon_deg = acos(r / (r + ALTITUDE_M)) / RADIO_LINK_D2R;
    ASSERT_NEAR(0.0, radio_link_elevation_deg(OVER_LON_0, IDENTITY, 0.0, horizon_deg), 1e-6);
}

static void test_elevation_applies_the_earth_fixed_transform(void)
{
    /* Inertial position fixed on +X; the Earth-fixed frame is flipped 180 degrees,
     * so the spacecraft is now over longitude 180. */
    ASSERT_NEAR(90.0, radio_link_elevation_deg(OVER_LON_0, FLIP_Z, 0.0, 180.0), 1e-6);
    TEST_ASSERT_TRUE(radio_link_elevation_deg(OVER_LON_0, FLIP_Z, 0.0, 0.0) < 0.0);
}

static void test_elevation_coincident_positions_do_not_divide_by_zero(void)
{
    double on_surface[3] = {RADIO_LINK_EARTH_RADIUS_M, 0, 0};
    ASSERT_NEAR(90.0, radio_link_elevation_deg(on_surface, IDENTITY, 0.0, 0.0), 1e-9);
}

/* ------------------------------------------------------------------------- */
static void test_reset_applies_configured_defaults(void)
{
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_MODE_AUTO, g_link.mode);
    TEST_ASSERT_EQUAL_UINT64(250000000ULL, g_link.delay_ns[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT64(500000000ULL, g_link.delay_ns[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT32(1000, g_link.drop_ppm[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(2000, g_link.drop_ppm[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT8(1, g_link.impairments_enabled);
    TEST_ASSERT_EQUAL_UINT32(3, g_link.num_stations);
    TEST_ASSERT_EQUAL_UINT8(0, g_link.up); /* auto starts down without geometry */
}

static void test_reset_restores_settings_and_clears_queues_and_counters(void)
{
    g_link.mode = RADIO_LINK_MODE_NEVER;
    g_link.delay_ns[RADIO_LINK_UP] = 1;
    g_link.drop_ppm[RADIO_LINK_DOWN] = 999999;
    g_link.stations[0].enabled = 0;
    g_link.dropped[RADIO_LINK_UP] = 7;
    TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_UP, 10, (const uint8_t *)"x", 1));

    radio_link_reset(&g_link);

    TEST_ASSERT_EQUAL_INT(RADIO_LINK_MODE_AUTO, g_link.mode);
    TEST_ASSERT_EQUAL_UINT64(250000000ULL, g_link.delay_ns[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(2000, g_link.drop_ppm[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT8(1, g_link.stations[0].enabled);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.dropped[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.queue[RADIO_LINK_UP].count);
}

/* ------------------------------------------------------------------------- */
static void test_update_always_and_never_ignore_geometry(void)
{
    g_link.mode = RADIO_LINK_MODE_ALWAYS;
    TEST_ASSERT_TRUE(radio_link_update(&g_link, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(1, g_link.up);
    TEST_ASSERT_FALSE(radio_link_update(&g_link, OVER_LON_0, IDENTITY));

    g_link.mode = RADIO_LINK_MODE_NEVER;
    TEST_ASSERT_TRUE(radio_link_update(&g_link, OVER_LON_0, IDENTITY)); /* directly overhead, still down */
    TEST_ASSERT_EQUAL_UINT8(0, g_link.up);
    TEST_ASSERT_FALSE(radio_link_update(&g_link, OVER_LON_0, IDENTITY));
}

static void test_update_auto_follows_geometry_and_reports_transitions(void)
{
    TEST_ASSERT_TRUE(radio_link_update(&g_link, OVER_LON_0, IDENTITY)); /* station 0 sees it */
    TEST_ASSERT_EQUAL_UINT8(1, g_link.up);
    TEST_ASSERT_FALSE(radio_link_update(&g_link, OVER_LON_0, IDENTITY));

    TEST_ASSERT_TRUE(radio_link_update(&g_link, OVER_LON_0, FLIP_Z)); /* over 180 E: nobody sees it */
    TEST_ASSERT_EQUAL_UINT8(0, g_link.up);
}

static void test_update_auto_holds_state_without_geometry(void)
{
    radio_link_update(&g_link, OVER_LON_0, IDENTITY);
    TEST_ASSERT_EQUAL_UINT8(1, g_link.up);
    TEST_ASSERT_FALSE(radio_link_update(&g_link, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(1, g_link.up);
}

static void test_update_auto_any_enabled_station_is_enough(void)
{
    g_link.stations[0].enabled = 0; /* the one overhead */
    radio_link_update(&g_link, OVER_LON_0, IDENTITY);
    /* Station 2 is only 10 degrees away but needs 60 degrees elevation; station 1 is on the far side. */
    TEST_ASSERT_EQUAL_UINT8(0, g_link.up);

    g_link.stations[2].min_elev_deg = 5.0;
    radio_link_update(&g_link, OVER_LON_0, IDENTITY);
    TEST_ASSERT_EQUAL_UINT8(1, g_link.up);
}

static void test_update_auto_with_no_stations_is_never_in_view(void)
{
    g_link.num_stations = 0;
    radio_link_update(&g_link, OVER_LON_0, IDENTITY);
    TEST_ASSERT_EQUAL_UINT8(0, g_link.up);
}

/* ------------------------------------------------------------------------- */
static void test_admit_out_of_view_counts_and_blocks(void)
{
    g_link.up = 0;
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_ADMIT_OUT_OF_VIEW, radio_link_admit(&g_link, RADIO_LINK_UP));
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_ADMIT_OUT_OF_VIEW, radio_link_admit(&g_link, RADIO_LINK_DOWN));
    TEST_ASSERT_EQUAL_UINT32(1, g_link.out_of_view[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(1, g_link.out_of_view[RADIO_LINK_DOWN]);
}

static void test_admit_zero_and_total_loss(void)
{
    g_link.up = 1;
    g_link.drop_ppm[RADIO_LINK_UP] = 0;
    g_link.drop_ppm[RADIO_LINK_DOWN] = RADIO_LINK_PPM_SCALE;
    for (int i = 0; i < 1000; i++)
    {
        TEST_ASSERT_EQUAL_INT(RADIO_LINK_ADMIT_OK, radio_link_admit(&g_link, RADIO_LINK_UP));
        TEST_ASSERT_EQUAL_INT(RADIO_LINK_ADMIT_DROPPED, radio_link_admit(&g_link, RADIO_LINK_DOWN));
    }
    TEST_ASSERT_EQUAL_UINT32(1000, g_link.dropped[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.dropped[RADIO_LINK_UP]);
}

static void test_admit_loss_rate_tracks_ppm(void)
{
    const uint32_t trials = 200000;
    g_link.up = 1;
    g_link.drop_ppm[RADIO_LINK_UP] = 100000; /* 10 % */
    for (uint32_t i = 0; i < trials; i++) radio_link_admit(&g_link, RADIO_LINK_UP);
    /* Expect 20000 +- a generous 5 sigma (~670). */
    TEST_ASSERT_UINT32_WITHIN(1000, 20000, g_link.dropped[RADIO_LINK_UP]);
}

static void test_admit_is_deterministic_for_a_seed(void)
{
    uint8_t first[256], second[256];
    g_link.up = 1;
    g_link.drop_ppm[RADIO_LINK_UP] = 500000;

    radio_link_seed(&g_link, 777);
    for (int i = 0; i < 256; i++) first[i] = (uint8_t)radio_link_admit(&g_link, RADIO_LINK_UP);
    radio_link_seed(&g_link, 777);
    for (int i = 0; i < 256; i++) second[i] = (uint8_t)radio_link_admit(&g_link, RADIO_LINK_UP);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, second, 256);

    radio_link_seed(&g_link, 778);
    int differs = 0;
    for (int i = 0; i < 256; i++) differs |= first[i] != (uint8_t)radio_link_admit(&g_link, RADIO_LINK_UP);
    TEST_ASSERT_TRUE(differs);
}

static void test_zero_seed_does_not_lock_up_the_generator(void)
{
    g_link.up = 1;
    g_link.drop_ppm[RADIO_LINK_UP] = 500000;
    radio_link_seed(&g_link, 0);
    for (int i = 0; i < 200; i++) radio_link_admit(&g_link, RADIO_LINK_UP);
    TEST_ASSERT_TRUE(g_link.dropped[RADIO_LINK_UP] > 0);
    TEST_ASSERT_TRUE(g_link.dropped[RADIO_LINK_UP] < 200);
}

static void test_disabling_impairments_bypasses_loss_and_delay_but_not_visibility(void)
{
    g_link.up = 1;
    g_link.drop_ppm[RADIO_LINK_UP] = RADIO_LINK_PPM_SCALE;
    g_link.impairments_enabled = 0;
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_ADMIT_OK, radio_link_admit(&g_link, RADIO_LINK_UP));
    TEST_ASSERT_EQUAL_UINT64(0, radio_link_delay_ns(&g_link, RADIO_LINK_DOWN));

    g_link.up = 0;
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_ADMIT_OUT_OF_VIEW, radio_link_admit(&g_link, RADIO_LINK_UP));

    g_link.impairments_enabled = 1;
    TEST_ASSERT_EQUAL_UINT64(500000000ULL, radio_link_delay_ns(&g_link, RADIO_LINK_DOWN));
}

/* ------------------------------------------------------------------------- */
static void test_queue_releases_in_order_when_due(void)
{
    const uint8_t a[] = {1, 2, 3}, b[] = {4};
    TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_DOWN, 100, a, sizeof(a)));
    TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_DOWN, 200, b, sizeof(b)));

    TEST_ASSERT_NULL(radio_link_peek_due(&g_link, RADIO_LINK_DOWN, 99));
    const radio_link_packet_t *p = radio_link_peek_due(&g_link, RADIO_LINK_DOWN, 100);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(3, p->len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, p->data, 3);
    radio_link_pop(&g_link, RADIO_LINK_DOWN);

    TEST_ASSERT_NULL(radio_link_peek_due(&g_link, RADIO_LINK_DOWN, 150));
    p = radio_link_peek_due(&g_link, RADIO_LINK_DOWN, 1000);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT8(4, p->data[0]);
    radio_link_pop(&g_link, RADIO_LINK_DOWN);
    TEST_ASSERT_NULL(radio_link_peek_due(&g_link, RADIO_LINK_DOWN, 1000));
}

static void test_queue_is_head_of_line_so_order_survives_a_shortened_delay(void)
{
    const uint8_t x = 0;
    radio_link_enqueue(&g_link, RADIO_LINK_UP, 500, &x, 1);
    radio_link_enqueue(&g_link, RADIO_LINK_UP, 100, &x, 1); /* later packet, earlier release */
    TEST_ASSERT_NULL(radio_link_peek_due(&g_link, RADIO_LINK_UP, 100));
    TEST_ASSERT_NOT_NULL(radio_link_peek_due(&g_link, RADIO_LINK_UP, 500));
}

static void test_queue_overflow_is_counted_and_directions_are_independent(void)
{
    const uint8_t x = 9;
    for (int i = 0; i < RADIO_CFG_LINK_QUEUE_DEPTH; i++)
        TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_UP, 1, &x, 1));
    TEST_ASSERT_FALSE(radio_link_enqueue(&g_link, RADIO_LINK_UP, 1, &x, 1));
    TEST_ASSERT_EQUAL_UINT32(1, g_link.overflowed[RADIO_LINK_UP]);
    TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_DOWN, 1, &x, 1));
    TEST_ASSERT_EQUAL_UINT32(0, g_link.overflowed[RADIO_LINK_DOWN]);
}

static void test_queue_allocation_failure_is_counted_as_overflow(void)
{
    const uint8_t x = 3;
    g_fail_malloc = 1;
    TEST_ASSERT_FALSE(radio_link_enqueue(&g_link, RADIO_LINK_DOWN, 0, &x, 1));
    g_fail_malloc = 0;
    TEST_ASSERT_EQUAL_UINT32(1, g_link.overflowed[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_DOWN, 0, &x, 1));
}

static void test_queue_wraps_around_the_ring(void)
{
    const uint8_t x = 5;
    for (int round = 0; round < 3 * RADIO_CFG_LINK_QUEUE_DEPTH; round++)
    {
        TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_DOWN, (uint64_t)round, &x, 1));
        const radio_link_packet_t *p = radio_link_peek_due(&g_link, RADIO_LINK_DOWN, (uint64_t)round);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_UINT64((uint64_t)round, p->release_ns);
        radio_link_pop(&g_link, RADIO_LINK_DOWN);
    }
    TEST_ASSERT_EQUAL_UINT32(0, g_link.queue[RADIO_LINK_DOWN].count);
}

static void test_queue_accepts_empty_packets_and_pop_on_empty_is_safe(void)
{
    radio_link_pop(&g_link, RADIO_LINK_UP);
    TEST_ASSERT_TRUE(radio_link_enqueue(&g_link, RADIO_LINK_UP, 0, NULL, 0));
    const radio_link_packet_t *p = radio_link_peek_due(&g_link, RADIO_LINK_UP, 0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(0, p->len);
}

static void test_corrupt_flips_one_bit_in_the_requested_number_of_packets(void)
{
    uint8_t a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0};
    g_link.corrupt_remaining[RADIO_LINK_UP] = 2;
    g_link.corrupt_offset[RADIO_LINK_UP] = 2;

    TEST_ASSERT_TRUE(radio_link_corrupt(&g_link, RADIO_LINK_UP, a, sizeof(a)));
    TEST_ASSERT_TRUE(radio_link_corrupt(&g_link, RADIO_LINK_UP, b, sizeof(b)));
    TEST_ASSERT_FALSE(radio_link_corrupt(&g_link, RADIO_LINK_UP, c, sizeof(c)));
    TEST_ASSERT_EQUAL_UINT8(1, a[2]);
    TEST_ASSERT_EQUAL_UINT8(1, b[2]);
    TEST_ASSERT_EQUAL_UINT8(0, c[2]);
    TEST_ASSERT_EQUAL_UINT8(0, a[0] | a[1] | a[3]);
    TEST_ASSERT_EQUAL_UINT32(2, g_link.corrupted[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.corrupted[RADIO_LINK_DOWN]);
}

static void test_corrupt_waits_for_a_packet_long_enough_and_directions_are_independent(void)
{
    uint8_t shorter[2] = {0, 0}, longer[8] = {0};
    g_link.corrupt_remaining[RADIO_LINK_DOWN] = 1;
    g_link.corrupt_offset[RADIO_LINK_DOWN] = 5;

    TEST_ASSERT_FALSE(radio_link_corrupt(&g_link, RADIO_LINK_UP, longer, sizeof(longer)));
    TEST_ASSERT_FALSE(radio_link_corrupt(&g_link, RADIO_LINK_DOWN, shorter, sizeof(shorter)));
    TEST_ASSERT_EQUAL_UINT16(1, g_link.corrupt_remaining[RADIO_LINK_DOWN]);
    TEST_ASSERT_TRUE(radio_link_corrupt(&g_link, RADIO_LINK_DOWN, longer, sizeof(longer)));
    TEST_ASSERT_EQUAL_UINT8(1, longer[5]);
}

static void test_replay_returns_the_last_remembered_packet(void)
{
    uint8_t out[RADIO_LINK_MAX_PACKET];
    const uint8_t first[] = {1, 2, 3}, second[] = {9, 8};

    TEST_ASSERT_EQUAL_UINT32(0, radio_link_replay_copy(&g_link, RADIO_LINK_UP, out));
    radio_link_remember(&g_link, RADIO_LINK_UP, first, sizeof(first));
    radio_link_remember(&g_link, RADIO_LINK_UP, second, sizeof(second));

    TEST_ASSERT_EQUAL_UINT32(2, radio_link_replay_copy(&g_link, RADIO_LINK_UP, out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, out, 2);
    TEST_ASSERT_EQUAL_UINT32(1, g_link.replayed[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(0, radio_link_replay_copy(&g_link, RADIO_LINK_DOWN, out));
}

static void test_remember_ignores_empty_and_oversized_packets_and_reset_forgets(void)
{
    static uint8_t big[RADIO_LINK_MAX_PACKET + 1];
    uint8_t out[RADIO_LINK_MAX_PACKET];
    const uint8_t keep[] = {7};

    radio_link_remember(&g_link, RADIO_LINK_UP, keep, sizeof(keep));
    radio_link_remember(&g_link, RADIO_LINK_UP, big, sizeof(big));
    radio_link_remember(&g_link, RADIO_LINK_UP, keep, 0);
    TEST_ASSERT_EQUAL_UINT32(1, radio_link_replay_copy(&g_link, RADIO_LINK_UP, out));
    TEST_ASSERT_EQUAL_UINT8(7, out[0]);

    g_link.corrupt_remaining[RADIO_LINK_UP] = 3;
    radio_link_reset(&g_link);
    TEST_ASSERT_EQUAL_UINT32(0, radio_link_replay_copy(&g_link, RADIO_LINK_UP, out));
    TEST_ASSERT_EQUAL_UINT16(0, g_link.corrupt_remaining[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.replayed[RADIO_LINK_UP]);
}

static void test_clear_queues_keeps_settings(void)
{
    const uint8_t x = 1;
    radio_link_enqueue(&g_link, RADIO_LINK_UP, 0, &x, 1);
    radio_link_enqueue(&g_link, RADIO_LINK_DOWN, 0, &x, 1);
    g_link.drop_ppm[RADIO_LINK_UP] = 42;
    radio_link_clear_queues(&g_link);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.queue[RADIO_LINK_UP].count);
    TEST_ASSERT_EQUAL_UINT32(0, g_link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_EQUAL_UINT32(42, g_link.drop_ppm[RADIO_LINK_UP]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_elevation_zenith_is_90_degrees);
    RUN_TEST(test_elevation_far_side_is_negative);
    RUN_TEST(test_elevation_decreases_with_distance_from_subpoint);
    RUN_TEST(test_elevation_horizon_matches_geometry);
    RUN_TEST(test_elevation_applies_the_earth_fixed_transform);
    RUN_TEST(test_elevation_coincident_positions_do_not_divide_by_zero);
    RUN_TEST(test_reset_applies_configured_defaults);
    RUN_TEST(test_reset_restores_settings_and_clears_queues_and_counters);
    RUN_TEST(test_update_always_and_never_ignore_geometry);
    RUN_TEST(test_update_auto_follows_geometry_and_reports_transitions);
    RUN_TEST(test_update_auto_holds_state_without_geometry);
    RUN_TEST(test_update_auto_any_enabled_station_is_enough);
    RUN_TEST(test_update_auto_with_no_stations_is_never_in_view);
    RUN_TEST(test_admit_out_of_view_counts_and_blocks);
    RUN_TEST(test_admit_zero_and_total_loss);
    RUN_TEST(test_admit_loss_rate_tracks_ppm);
    RUN_TEST(test_admit_is_deterministic_for_a_seed);
    RUN_TEST(test_zero_seed_does_not_lock_up_the_generator);
    RUN_TEST(test_disabling_impairments_bypasses_loss_and_delay_but_not_visibility);
    RUN_TEST(test_queue_releases_in_order_when_due);
    RUN_TEST(test_queue_is_head_of_line_so_order_survives_a_shortened_delay);
    RUN_TEST(test_queue_overflow_is_counted_and_directions_are_independent);
    RUN_TEST(test_queue_allocation_failure_is_counted_as_overflow);
    RUN_TEST(test_queue_wraps_around_the_ring);
    RUN_TEST(test_queue_accepts_empty_packets_and_pop_on_empty_is_safe);
    RUN_TEST(test_corrupt_flips_one_bit_in_the_requested_number_of_packets);
    RUN_TEST(test_corrupt_waits_for_a_packet_long_enough_and_directions_are_independent);
    RUN_TEST(test_replay_returns_the_last_remembered_packet);
    RUN_TEST(test_remember_ignores_empty_and_oversized_packets_and_reset_forgets);
    RUN_TEST(test_clear_queues_keeps_settings);
    return UNITY_END();
}
