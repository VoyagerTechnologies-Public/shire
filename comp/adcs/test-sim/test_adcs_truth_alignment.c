/*
 * Contract test: confirms 42-truth telemetry (SIM_42_TRUTH, produced by
 * simulith_serialize_42_telemetry()) and ADCS HK telemetry (produced by
 * adcs_sim's on_tick()) stay bit-identical (modulo float64->float32
 * truncation) for the fields they share, since both are populated from the
 * same simulith_42_context_t snapshot each tick
 * (simulith/src/simulith_director.c: populate_42_context() /
 * director_commit_tick(); comp/adcs/sim/adcs_sim.c:
 * adcs_sim_component_on_tick()).
 *
 * See atlas/docs/scenarios/adcs-truth-comparison.md,
 * which left "is ADCS telemetry numerically consistent with 42 truth"
 * unresolved. This test answers that for the shared fields at the source;
 * it does not (and cannot, at this unit level) confirm wire/ground-side
 * frame or timing alignment end to end -- that is Tier B (see the plan).
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "simulith_component.h"
#include "simulith_42_context.h"
#include "simulith_director.h"

#include "adcs_sim.h"
#include "adcs_device.h"

#ifndef ADCS_SIM_SO_PATH
#error "ADCS_SIM_SO_PATH must be defined (path to adcs_sim.so)"
#endif

/* Named tolerances (never bare literals in the assertions below) so a
 * future Monte Carlo scorer can re-parameterize these per trial instead of
 * hardcoding a new copy. */
#define UNIT_VECTOR_ABS_TOL 1e-5f
#define RATE_ABS_TOL_RAD_S  1e-6f
#define POSITION_ABS_TOL_M  1.0f

typedef const component_interface_t *(*get_component_interface_fn)(void);

static void       *g_handle = NULL;
static const component_interface_t *g_iface = NULL;
static component_state_t *g_state_under_test = NULL;

void setUp(void) {}

void tearDown(void)
{
    if (g_state_under_test)
    {
        g_iface->destroy(g_state_under_test);
        g_state_under_test = NULL;
    }
}

/* Mirrors simulith_serialize_42_telemetry()'s exact field order (see
 * simulith/src/simulith_director.c) so this test decodes the real wire
 * layout rather than asserting against a duplicated struct definition. */
typedef struct
{
    double dyn_time;
    double pos_n[3];
    double sun_vector_body[3];
    double wn[3];
    double qn[4];
    int    eclipse;
} decoded_truth_fields_t;

static void decode_truth_packet(const uint8_t *packet, decoded_truth_fields_t *out)
{
    size_t offset = 0;
    memcpy(&out->dyn_time, packet + offset, sizeof(double));
    offset += sizeof(double);
    memcpy(out->pos_n, packet + offset, 3 * sizeof(double));
    offset += 3 * sizeof(double);
    memcpy(out->sun_vector_body, packet + offset, 3 * sizeof(double));
    offset += 3 * sizeof(double);
    offset += 3 * sizeof(double); /* mag_field_body: not needed by this test */
    offset += 3 * sizeof(double); /* hvb: not needed by this test */
    memcpy(out->wn, packet + offset, 3 * sizeof(double));
    offset += 3 * sizeof(double);
    memcpy(out->qn, packet + offset, 4 * sizeof(double));
    offset += 4 * sizeof(double);
    offset += sizeof(double);      /* mass */
    offset += 3 * sizeof(double);  /* cm */
    offset += 9 * sizeof(double);  /* inertia */
    memcpy(&out->eclipse, packet + offset, sizeof(int));
    offset += sizeof(int);
    /* atmo_density follows; not needed by this test. */
}

static simulith_42_context_t make_distinguishable_context(void)
{
    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.dyn_time = 778432100.25;
    ctx.eclipse  = 1;
    ctx.pos_n[0] = 6778100.0; ctx.pos_n[1] = -321000.5; ctx.pos_n[2] = 45210.75;
    ctx.vel_n[0] = 10.0; ctx.vel_n[1] = 20.0; ctx.vel_n[2] = 30.0;
    ctx.wn[0] = 0.0123; ctx.wn[1] = -0.0456; ctx.wn[2] = 0.0789;
    ctx.qn[0] = 0.1; ctx.qn[1] = 0.2; ctx.qn[2] = 0.3; ctx.qn[3] = -0.9273618;
    ctx.sun_vector_body[0] = 0.267261;
    ctx.sun_vector_body[1] = 0.534522;
    ctx.sun_vector_body[2] = 0.801784;
    ctx.mass = 4.0;
    return ctx;
}

/* Runs the context through both population paths and returns the decoded
 * truth fields plus the HK state, so each test just asserts on the pair. */
static void run_both_paths(const simulith_42_context_t *ctx,
                           decoded_truth_fields_t *truth_out,
                           adcs_sim_state_t **hk_state_out)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;

    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          g_iface->on_tick(state, 200000000ULL, ctx));

    uint8_t packet[SIMULITH_42_TELEMETRY_SIZE];
    TEST_ASSERT_EQUAL_UINT(SIMULITH_42_TELEMETRY_SIZE,
                           simulith_serialize_42_telemetry(ctx, packet, sizeof(packet)));
    decode_truth_packet(packet, truth_out);

    *hk_state_out = (adcs_sim_state_t *)state;
}

static void test_hk_sun_vector_matches_truth_svb_within_float32_tolerance(void)
{
    simulith_42_context_t ctx = make_distinguishable_context();
    decoded_truth_fields_t truth;
    adcs_sim_state_t *hk;
    run_both_paths(&ctx, &truth, &hk);

    for (int i = 0; i < 3; i++)
        TEST_ASSERT_FLOAT_WITHIN(UNIT_VECTOR_ABS_TOL,
                                (float)truth.sun_vector_body[i], hk->hk.SunVectorBody[i]);
}

static void test_hk_ang_rate_matches_truth_wn_within_float32_tolerance(void)
{
    simulith_42_context_t ctx = make_distinguishable_context();
    decoded_truth_fields_t truth;
    adcs_sim_state_t *hk;
    run_both_paths(&ctx, &truth, &hk);

    for (int i = 0; i < 3; i++)
        TEST_ASSERT_FLOAT_WITHIN(RATE_ABS_TOL_RAD_S,
                                (float)truth.wn[i], hk->hk.AngRate[i]);
}

static void test_hk_quaternion_matches_truth_qn_within_float32_tolerance(void)
{
    simulith_42_context_t ctx = make_distinguishable_context();
    decoded_truth_fields_t truth;
    adcs_sim_state_t *hk;
    run_both_paths(&ctx, &truth, &hk);

    for (int i = 0; i < 4; i++)
        TEST_ASSERT_FLOAT_WITHIN(UNIT_VECTOR_ABS_TOL,
                                (float)truth.qn[i], hk->hk.Quaternion[i]);
}

static void test_hk_gps_position_matches_truth_pos_n_within_float32_tolerance(void)
{
    simulith_42_context_t ctx = make_distinguishable_context();
    decoded_truth_fields_t truth;
    adcs_sim_state_t *hk;
    run_both_paths(&ctx, &truth, &hk);

    for (int i = 0; i < 3; i++)
        TEST_ASSERT_FLOAT_WITHIN(POSITION_ABS_TOL_M,
                                (float)truth.pos_n[i], hk->hk.GpsPosition[i]);
}

static void test_hk_eclipse_matches_truth_eclipse(void)
{
    simulith_42_context_t ctx = make_distinguishable_context();
    decoded_truth_fields_t truth;
    adcs_sim_state_t *hk;
    run_both_paths(&ctx, &truth, &hk);

    TEST_ASSERT_EQUAL_INT(truth.eclipse != 0, hk->hk.Eclipse != 0);
}

/* Pairing note (see the module doc comment above): both paths must be fed
 * dyn_time-keyed samples, never CCSDS generation time, until issue #8
 * items 5/6 (GPS-driven cFE time sync) land. This test doesn't touch CCSDS
 * headers, but asserts the dyn_time itself survives the truth packet
 * unmodified, which is what any future pairing logic must key off of. */
static void test_truth_packet_preserves_dyn_time(void)
{
    simulith_42_context_t ctx = make_distinguishable_context();
    decoded_truth_fields_t truth;
    adcs_sim_state_t *hk;
    run_both_paths(&ctx, &truth, &hk);

    /* Unity in this build is compiled without double-precision assertions
     * (matches the convention already used throughout test_adcs_sim.c),
     * so compare via float: dyn_time here is small/exact enough that the
     * float32 cast is still an exact-equality check in practice. */
    TEST_ASSERT_EQUAL_FLOAT((float)ctx.dyn_time, (float)truth.dyn_time);
    /* HK's own dyn_time-derived fields (GpsSeconds/GpsSubseconds) are
     * covered by the existing test_tick_populates_hk_from_42_context in
     * test_adcs_sim.c; not duplicated here. */
}

/* Explicit "can't test this" marker: these truth fields have no ADCS HK
 * counterpart at all today, so the gap is documented in the test run's
 * output (IGNORED), not just in a comment. If ADCS HK ever gains one of
 * these fields, replace this with a real comparison test instead of
 * deleting it. */
static void test_no_hk_counterpart_for_bvb_hvb_mass_inertia_cm_fields(void)
{
    TEST_IGNORE_MESSAGE(
        "ADCS_Device_HK_tlm_t has no mag_field_body/hvb/mass/cm/inertia "
        "fields -- SIM_42_TRUTH's BVB/HVB/MASS/CM/INERTIA_* parameters "
        "have no ADCS-side telemetry counterpart to cross-check yet.");
}

int main(void)
{
    g_handle = dlopen(ADCS_SIM_SO_PATH, RTLD_NOW);
    if (!g_handle)
    {
        fprintf(stderr, "Failed to dlopen %s: %s\n", ADCS_SIM_SO_PATH, dlerror());
        return 1;
    }

    get_component_interface_fn get_iface =
        (get_component_interface_fn)dlsym(g_handle, "get_component_interface");
    if (!get_iface)
    {
        fprintf(stderr, "Failed to dlsym(get_component_interface): %s\n", dlerror());
        dlclose(g_handle);
        return 1;
    }

    g_iface = get_iface();
    if (!g_iface)
    {
        fprintf(stderr, "get_component_interface() returned NULL\n");
        dlclose(g_handle);
        return 1;
    }

    UNITY_BEGIN();

    RUN_TEST(test_hk_sun_vector_matches_truth_svb_within_float32_tolerance);
    RUN_TEST(test_hk_ang_rate_matches_truth_wn_within_float32_tolerance);
    RUN_TEST(test_hk_quaternion_matches_truth_qn_within_float32_tolerance);
    RUN_TEST(test_hk_gps_position_matches_truth_pos_n_within_float32_tolerance);
    RUN_TEST(test_hk_eclipse_matches_truth_eclipse);
    RUN_TEST(test_truth_packet_preserves_dyn_time);
    RUN_TEST(test_no_hk_counterpart_for_bvb_hvb_mass_inertia_cm_fields);

    int result = UNITY_END();
    dlclose(g_handle);
    return result;
}
