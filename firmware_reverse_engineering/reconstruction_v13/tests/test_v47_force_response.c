/* Host-only checks for the v47 force-response model.
 *
 * Values come from 07_analysis/force_experiment_v47_20260830/, a 5-position
 * press experiment with 30 s holds (8588/8588 SWD reads, 111644 record
 * samples).  Nothing here touches MMIO. */

#include "../../07_analysis/coinft_firmware_pseudocode_v47.c"

#include <stdio.h>

static unsigned g_failures;

static void check(bool condition, const char *label)
{
    if (!condition) {
        ++g_failures;
        printf("FAIL: %s\n", label);
    }
}

/* Steady-state raw minus unloaded raw, per channel, for each pressed
 * quadrant.  Rounded from the analysis tool's output. */
static const int k_delta_ru[12] = {
    2110, 3434, -41, 311, 69, -136, 992, 1578, 2844, 104, -47, 1300 };
static const int k_delta_lu[12] = {
    145, 166, 187, 184, 920, 1246, -3, -122, 149, 171, 1033, -71 };
static const int k_delta_ld[12] = {
    125, -94, 1411, 662, 297, 603, -16, -276, -2, 1004, 423, -173 };
static const int k_delta_rd[12] = {
    507, 865, 42, 36, 17, -130, 1303, 1722, 689, 37, -67, 1562 };
static const int k_delta_center[12] = {
    4038, 3587, 3173, 3703, 3612, 3353, 3458, 2925, 3754, 3225, 3240, 3132 };

static void top3(const int *delta, unsigned *out)
{
    unsigned used[12] = {0};
    unsigned n, i, best;
    for (n = 0u; n < 3u; ++n) {
        best = 12u;
        for (i = 0u; i < 12u; ++i) {
            if (used[i])
                continue;
            if (best == 12u || delta[i] > delta[best])
                best = i;
        }
        used[best] = 1u;
        out[n] = best;
    }
}

static void check_quadrant(const int *delta, coinft_v47_quadrant_t expected,
                           const char *label)
{
    unsigned top[3];
    unsigned i;
    coinft_v47_quadrant_t q;
    top3(delta, top);
    for (i = 0u; i < 3u; ++i) {
        check(coinft_v47_channel_quadrant(top[i], &q) && q == expected, label);
    }
}

static void test_quadrant_mapping(void)
{
    check_quadrant(k_delta_ru, COINFT_V47_QUADRANT_RU, "RU press -> RU channels");
    check_quadrant(k_delta_lu, COINFT_V47_QUADRANT_LU, "LU press -> LU channels");
    check_quadrant(k_delta_ld, COINFT_V47_QUADRANT_LD, "LD press -> LD channels");
    check_quadrant(k_delta_rd, COINFT_V47_QUADRANT_RD, "RD press -> RD channels");

    /* A centre press loads every channel, so no single quadrant dominates. */
    unsigned i;
    int lo = k_delta_center[0], hi = k_delta_center[0];
    for (i = 1u; i < 12u; ++i) {
        if (k_delta_center[i] < lo) lo = k_delta_center[i];
        if (k_delta_center[i] > hi) hi = k_delta_center[i];
    }
    check(lo > 2000, "centre press drives every channel well above noise");
    check(hi - lo < lo, "centre press spread is small next to its magnitude");

    check(!coinft_v47_channel_quadrant(12u, NULL), "null quadrant rejected");
}

static void test_pin_pairs(void)
{
    unsigned partner = 0u;
    check(coinft_v47_channel_pin_partner(0u, &partner) && partner == 8u,
          "ch0 pairs with ch8");
    check(coinft_v47_channel_pin_partner(6u, &partner) && partner == 11u,
          "ch6 pairs with ch11");
    check(coinft_v47_channel_pin_partner(11u, &partner) && partner == 6u,
          "pairing is symmetric");
    check(!coinft_v47_channel_pin_partner(1u, &partner),
          "ch1 has no pin partner");
    check(!coinft_v47_channel_pin_partner(12u, &partner),
          "out-of-range channel rejected");

    /* The partner uses double the reference CDAC. */
    check(COINFT_V47_CDAC_SEL_RE_PARTNER ==
          2 * COINFT_V47_CDAC_SEL_RE_PRIMARY, "partner CDAC is 2x primary");
    /* And the record limit word scales the same way. */
    check(COINFT_V46_LIMIT_GROUP_HIGH == 2u * COINFT_V46_LIMIT_GROUP_LOW,
          "partner limit_word is 2x primary");

    /* Partners are not redundant: an off-centre press separates them. */
    check(k_delta_ru[0] != k_delta_ru[8], "RU press separates ch0 from ch8");
}

static void test_polarity_and_baseline(void)
{
    /* 右上角 ch1: unloaded 20318, held 23751, released 20323, baseline 20262. */
    check(coinft_v47_force_increases_raw(20318u, 23751u),
          "force raises the raw count");
    check(!coinft_v47_force_increases_raw(23751u, 20318u),
          "predicate is directional");

    coinft_v46_sensor_record_t held = {23751u, 20262u, 0u, 0u, 0x1E00u};
    coinft_v46_sensor_record_t rest = {20323u, 20262u, 0u, 0u, 0x1E00u};
    check(coinft_v47_force_signal(&held) > 3000,
          "loaded force signal is large and positive");
    check(coinft_v47_force_signal(&rest) < 100,
          "released force signal returns to near zero");
    check(coinft_v47_force_signal(NULL) == 0, "null record yields zero");

    const uint16_t frozen[5] = {20262u, 20262u, 20262u, 20262u, 20262u};
    const uint16_t moving[5] = {20262u, 20262u, 20270u, 20262u, 20262u};
    check(coinft_v47_baseline_frozen(frozen, 5), "baseline held across a hold");
    check(!coinft_v47_baseline_frozen(moving, 5), "a drifting baseline fails");
    check(!coinft_v47_baseline_frozen(NULL, 5), "null samples rejected");
    check(!coinft_v47_baseline_frozen(frozen, 0), "empty samples rejected");
}

static void test_unused_fields(void)
{
    coinft_v46_sensor_record_t loaded = {23751u, 20262u, 0u, 0u, 0x1E00u};
    coinft_v46_sensor_record_t hypothetical = {23751u, 20262u, 7u, 0u, 0x1E00u};
    check(coinft_v47_unused_fields_zero(&loaded),
          "field2/field3 stay zero under load");
    check(!coinft_v47_unused_fields_zero(&hypothetical),
          "a non-zero field2 would be detected");
    check(!coinft_v47_unused_fields_zero(NULL), "null record rejected");
}

int main(void)
{
    test_quadrant_mapping();
    test_pin_pairs();
    test_polarity_and_baseline();
    test_unused_fields();

    if (g_failures != 0u) {
        printf("test_v47_force_response: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v47_force_response: passed\n");
    return 0;
}
