/* Host-only checks for the v51 complete command table.
 *
 * These assertions are the guard rail v48 lacked: the cost of each command is
 * asserted in code, so a future round cannot rediscover by experiment that 'w'
 * blocks and writes flash. */

#include "../../07_analysis/coinft_firmware_pseudocode_v51.c"

#include <stdio.h>
#include <string.h>

static unsigned g_failures;

static void check(bool condition, const char *label)
{
    if (!condition) {
        ++g_failures;
        printf("FAIL: %s\n", label);
    }
}

static void test_read_only_set(void)
{
    /* Only these three may be sent without consequences. */
    check(coinft_v51_command_is_read_only('q'), "'q' is read-only");
    check(coinft_v51_command_is_read_only('s'), "'s' is read-only");
    check(coinft_v51_command_is_read_only('i'), "'i' is read-only");

    check(!coinft_v51_command_is_read_only('t'), "'t' is not read-only");
    check(!coinft_v51_command_is_read_only('w'), "'w' is not read-only");
    check(!coinft_v51_command_is_read_only('n'), "'n' is not read-only");
}

static void test_effects(void)
{
    unsigned t = coinft_v51_command_effects('t');
    unsigned w = coinft_v51_command_effects('w');
    unsigned n = coinft_v51_command_effects('n');

    check((t & COINFT_V51_EFFECT_BLOCKS) != 0u, "'t' blocks");
    check((t & COINFT_V51_EFFECT_SRAM_CONFIG) != 0u,
          "'t' rewrites a channel's scan parameter");
    check((t & COINFT_V51_EFFECT_FLASH_WRITE) == 0u,
          "'t' does not touch flash");

    check((w & COINFT_V51_EFFECT_BLOCKS) != 0u, "'w' blocks like 't'");
    check((w & COINFT_V51_EFFECT_FLASH_WRITE) != 0u, "'w' writes flash");

    check((n & COINFT_V51_EFFECT_FLASH_WRITE) != 0u,
          "'n' can write flash when the row is uninitialised");
    check((n & COINFT_V51_EFFECT_BLOCKS) == 0u, "'n' takes no parameters");

    /* An unknown byte must not be reported as dangerous. */
    check(coinft_v51_command_effects('Z') == COINFT_V51_EFFECT_NONE,
          "unknown commands have no recorded effect");
}

static void test_write_id_length(void)
{
    const uint8_t cr_first[4] = {0x0D, 'x', 'x', 'x'};
    const uint8_t nft[4] = {'N', 'F', 'T', 0x0D};
    const uint8_t no_cr[16] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'};

    /* Opcode plus the CR itself. */
    check(coinft_v51_write_id_length(cr_first, 4u) == 2u,
          "an immediate CR ends 'w' after two bytes");
    check(coinft_v51_write_id_length(nft, 4u) == 5u,
          "'NFT\\r' costs opcode + 4 bytes");
    /* Without a CR the loop runs to the 12-byte cap. */
    check(coinft_v51_write_id_length(no_cr, 16u) == 13u,
          "with no CR, 'w' consumes the full 12-byte cap");
    check(coinft_v51_write_id_length(NULL, 4u) == 1u, "null payload");

    check(COINFT_V51_WRITE_ID_MAX == 12 && COINFT_V51_CR == 0x0D,
          "'w' terminates on CR or 12 bytes");
}

static void test_fall_through(void)
{
    check(coinft_v51_write_falls_through_to_read('w'),
          "'w' falls through to 'n' with no break");
    check(!coinft_v51_write_falls_through_to_read('n'),
          "'n' does not fall through");
    check(!coinft_v51_write_falls_through_to_read('q'), "'q' does not");
}

static void test_scb_offsets(void)
{
    /* Derived from the firmware's uint32-indexed accesses. */
    check(COINFT_V51_SCB_TX_FIFO_STATUS == 0x82 * 4,
          "param_1[0x82] is TX_FIFO_STATUS at 0x208");
    check(COINFT_V51_SCB_TX_FIFO_WR == 0x90 * 4,
          "param_1[0x90] is TX_FIFO_WR at 0x240");
    check(COINFT_V51_SCB_RX_FIFO_RD == 0xD0 * 4,
          "param_1[0xd0] is RX_FIFO_RD at 0x340");
    check(COINFT_V51_QUERY_REPLY == 0x1A, "'q' replies with 0x1a");
    check(COINFT_V51_RX_EMPTY == 0xFFFFFFFFu,
          "an empty RX FIFO reads as 0xffffffff");
}


static void test_baseline_rule(void)
{
    /* Stage I: the baseline is inert unless tared. */
    check(!coinft_v51_baseline_changes_autonomously(),
          "the baseline never moves on its own during streaming");

    /* The tare ignores the index; the scan-parameter write does not. */
    check(coinft_v51_tare_applies(0xFFu),
          "a rejected index still tares every channel");
    check(coinft_v51_tare_applies(5u), "a valid index also tares");
    check(!coinft_v51_scan_parameter_applies(0xFFu),
          "index 0xff writes no scan parameter");
    check(!coinft_v51_scan_parameter_applies(12u),
          "index 12 is out of range for the 12 UART channels");
    check(coinft_v51_scan_parameter_applies(5u), "index 5 writes ch5");

    /* Residuals measured right after a live tare. */
    {
        const int32_t observed[6] = {+4, +2, -7, -10, -8, -2};
        unsigned i;
        for (i = 0u; i < 6u; ++i)
            check(coinft_v51_tare_residual_ok(observed[i]),
                  "observed post-tare residuals are within noise");
    }
    check(!coinft_v51_tare_residual_ok(3000),
          "a loaded channel is not a valid post-tare residual");
}

static void test_pending_block(void)
{
    /* Offsets confirmed in flash after writing "CFT-A1\r" to address 0x40. */
    check(COINFT_V51_BLOCK_ADDRESS == 0x08 &&
          COINFT_V51_BLOCK_LENGTH == 0x0C &&
          COINFT_V51_BLOCK_DATA == 0x10,
          "pending-write block layout matches the flash contents");

    /* The read path overlays a block only where the ranges intersect. */
    check(coinft_v51_block_covers(0x40u, 0x0Cu, 0x40u, 0x0Cu),
          "an exact match overlaps");
    check(coinft_v51_block_covers(0x40u, 0x0Cu, 0x44u, 0x04u),
          "a contained request overlaps");
    check(!coinft_v51_block_covers(0x40u, 0x0Cu, 0x4Cu, 0x04u),
          "an adjacent range does not overlap");
    check(!coinft_v51_block_covers(0x40u, 0x0Cu, 0x00u, 0x40u),
          "a range ending at the block start does not overlap");
    check(coinft_v51_block_covers(0x40u, 0x0Cu, 0x00u, 0x41u),
          "a range crossing into the block overlaps");
}

int main(void)
{
    test_read_only_set();
    test_effects();
    test_write_id_length();
    test_fall_through();
    test_scb_offsets();
    test_baseline_rule();
    test_pending_block();

    if (g_failures != 0u) {
        printf("test_v51_command_table: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v51_command_table: passed\n");
    return 0;
}
