/* v52 stage J: replay every recorded hardware observation through the
 * simulator and require the model to reproduce it.
 *
 * These are not invented cases.  Each block cites the round and capture that
 * produced the numbers, so a future change to the model that contradicts the
 * hardware fails here rather than in a document nobody re-reads. */

#include "../../07_analysis/coinft_sim_v52.c"

#include <stdio.h>

static unsigned g_failures;

static void check(bool condition, const char *label)
{
    if (!condition) {
        ++g_failures;
        printf("FAIL: %s\n", label);
    }
}

/* v46: idle shows INTR 0; streaming shows INIT, sometimes with CONFIG_REQ. */
static void test_mode_transitions(void)
{
    coinft_sim_t s;
    const uint16_t raw[13] = {19777, 20477, 24246, 21286, 23037, 25416,
                              16616, 16820, 19257, 23753, 25071, 15892, 2048};
    coinft_sim_reset(&s);
    check(!coinft_sim_scanning(&s), "a fresh device is not scanning");
    check(s.msc_intr == 0u, "idle INTR is 0");

    coinft_sim_feed_byte(&s, 's');
    check(coinft_sim_scanning(&s), "'s' starts scanning");
    coinft_sim_scan(&s, raw, 13);
    check(s.msc_intr != 0u, "a completed scan raises INTR");

    coinft_sim_feed_byte(&s, 'i');
    check(!coinft_sim_scanning(&s), "'i' stops scanning");
    check(s.msc_intr == 0u, "idle clears INTR");

    /* Records must not advance while idle: this is what made the v48 stall
     * observable as frozen values rather than as silence. */
    const uint16_t other[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 2048};
    coinft_sim_scan(&s, other, 13);
    check(s.raw[0] == 19777u, "an idle device does not update its records");
}

/* v48: a bare 't' parks the main loop; four more bytes release it. */
static void test_blocking(void)
{
    coinft_sim_t s;
    coinft_sim_reset(&s);
    coinft_sim_feed_byte(&s, 's');

    check(!coinft_sim_feed_byte(&s, 't'), "'t' does not complete alone");
    check(coinft_sim_stale(&s), "a blocked device transmits stale data");
    check(!coinft_sim_scanning(&s), "a blocked device stops scanning");
    check(s.msc_intr == 0u, "INTR falls to 0 while blocked, as observed");

    check(!coinft_sim_feed_byte(&s, 0xFFu), "1 of 4 parameter bytes");
    check(!coinft_sim_feed_byte(&s, 0x00u), "2 of 4");
    check(!coinft_sim_feed_byte(&s, 0x00u), "3 of 4");
    check(coinft_sim_feed_byte(&s, 0x00u), "the fourth byte completes 't'");
    check(coinft_sim_scanning(&s), "scanning resumes after the parameters");
    check(!coinft_sim_stale(&s), "no longer stale");
}

/* v51 experiment 1: index 0xff is rejected for the write yet still tares. */
static void test_tare_is_unconditional(void)
{
    coinft_sim_t s;
    const uint16_t loaded[13] = {23751, 20477, 24246, 21286, 23037, 25416,
                                 16616, 16820, 19257, 23753, 25071, 15892,
                                 2048};
    unsigned i;
    coinft_sim_reset(&s);
    coinft_sim_feed_byte(&s, 's');
    coinft_sim_scan(&s, loaded, 13);
    coinft_sim_tare(&s);

    /* Load one channel further, then tare via a rejected index. */
    coinft_sim_t before = s;
    const uint16_t pressed[13] = {26000, 20477, 24246, 21286, 23037, 25416,
                                  16616, 16820, 19257, 23753, 25071, 15892,
                                  2048};
    coinft_sim_scan(&s, pressed, 13);
    check(coinft_sim_force_signal(&s, 0) > 2000,
          "loading a channel produces a large force signal");

    coinft_sim_feed_byte(&s, 't');
    coinft_sim_feed_byte(&s, 0xFFu);        /* rejected index */
    coinft_sim_feed_byte(&s, 0x00u);
    coinft_sim_feed_byte(&s, 0x00u);
    coinft_sim_feed_byte(&s, 0x00u);

    for (i = 0u; i < 13u; ++i) {
        check(coinft_sim_tare_residual_ok_helper(&s, i),
              "every record is tared even though the index was rejected");
    }
    check(s.scan_param[0] == before.scan_param[0],
          "a rejected index writes no scan parameter");
    check(s.tares == before.tares + 1u, "exactly one tare occurred");
}

/* v51 experiment 2: a valid index writes the parameter and tares. */
static void test_valid_index_writes(void)
{
    coinft_sim_t s;
    const uint16_t raw[13] = {19777, 20477, 24246, 21286, 23037, 25416,
                              16616, 16820, 19257, 23753, 25071, 15892, 2048};
    coinft_sim_reset(&s);
    coinft_sim_feed_byte(&s, 's');
    coinft_sim_scan(&s, raw, 13);

    coinft_sim_feed_byte(&s, 't');
    coinft_sim_feed_byte(&s, 5u);
    coinft_sim_feed_byte(&s, 0x30u);
    coinft_sim_feed_byte(&s, 0x75u);        /* 30000 */
    coinft_sim_feed_byte(&s, 0x00u);
    check(s.scan_param[5] == 30000u, "ch5's scan parameter was written");
    check(s.scan_param[4] == 0u, "neighbouring channels are untouched");
    check(s.tares == 1u, "the tare still happened");
}

/* v51: 'q' replies with one byte; 'w' and 'n' touch flash. */
static void test_command_side_effects(void)
{
    coinft_sim_t s;
    coinft_sim_reset(&s);
    coinft_sim_feed_byte(&s, 's');

    check(coinft_sim_feed_byte(&s, 'q'), "'q' completes immediately");
    check(s.reply_len == 1u && s.reply[0] == 0x1Au, "'q' replies 0x1a");
    check(s.flash_writes == 0u, "'q' does not touch flash");

    check(coinft_sim_feed_byte(&s, 'n'), "'n' completes immediately");
    check(s.flash_writes == 1u, "'n' can write flash");

    /* 'w' blocks until CR, then writes and falls through to 'n'. */
    unsigned before = s.flash_writes;
    check(!coinft_sim_feed_byte(&s, 'w'), "'w' blocks");
    check(!coinft_sim_feed_byte(&s, 'C'), "payload byte");
    check(coinft_sim_feed_byte(&s, 0x0Du), "CR terminates 'w' early");
    check(s.flash_writes == before + 2u,
          "'w' writes and then falls through to 'n'");
}

/* v46/v47: the frame carries channels 0..11 only, little endian. */
static void test_frame_encoding(void)
{
    coinft_sim_t s;
    const uint16_t raw[13] = {0x1234, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                              0xABCD, 0x0800};
    uint8_t frame[26];
    coinft_sim_reset(&s);
    coinft_sim_feed_byte(&s, 's');
    coinft_sim_scan(&s, raw, 13);

    check(coinft_sim_pack_frame(&s, frame, sizeof frame), "frame packs");
    check(frame[0] == 0x02u && frame[25] == 0x03u, "SOF and EOF");
    check(frame[1] == 0x34u && frame[2] == 0x12u, "channel 0 little endian");
    check(frame[23] == 0xCDu && frame[24] == 0xABu, "channel 11 little endian");
    /* Record 12 must not appear anywhere in the payload. */
    unsigned i;
    bool leaked = false;
    for (i = 1u; i + 1u < 25u; i += 2u) {
        uint16_t v = (uint16_t)(frame[i] | ((uint16_t)frame[i + 1] << 8));
        if (v == 0x0800u)
            leaked = true;
    }
    check(!leaked, "record 12 never reaches the wire");
    check(!coinft_sim_pack_frame(&s, frame, 25u), "wrong length rejected");
}

int main(void)
{
    test_mode_transitions();
    test_blocking();
    test_tare_is_unconditional();
    test_valid_index_writes();
    test_command_side_effects();
    test_frame_encoding();

    if (g_failures != 0u) {
        printf("test_v52_conformance: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v52_conformance: passed\n");
    return 0;
}
