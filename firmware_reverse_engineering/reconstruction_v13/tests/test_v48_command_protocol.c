/* Host-only checks for the v48 command protocol model.
 *
 * The stall behaviour asserted here was reproduced on hardware: sending a bare
 * 't' drove MSC INTR to 0 and froze the records; supplying four more bytes with
 * index 0xff restored INTR=0x01001000 immediately. */

#include "../../07_analysis/coinft_firmware_pseudocode_v48.c"

#include <stdio.h>

static unsigned g_failures;

static void check(bool condition, const char *label)
{
    if (!condition) {
        ++g_failures;
        printf("FAIL: %s\n", label);
    }
}

static void test_command_lengths(void)
{
    check(coinft_v48_command_length('s') == 1u, "'s' is one byte");
    check(coinft_v48_command_length('i') == 1u, "'i' is one byte");
    check(coinft_v48_command_length('q') == 1u, "'q' is one byte");
    check(coinft_v48_command_length('w') == 1u, "'w' opcode is one byte");
    check(coinft_v48_command_length('n') == 1u, "'n' is one byte");
    check(coinft_v48_command_length('t') == 5u, "'t' needs opcode + 4 params");
}

static void test_stall_predicate(void)
{
    /* This is the mistake v48 made against real hardware. */
    check(coinft_v48_command_stalls_main_loop('t', 1u),
          "bare 't' stalls the main loop");
    check(!coinft_v48_command_stalls_main_loop('t', 5u),
          "'t' with four params does not stall");
    check(coinft_v48_command_stalls_main_loop('t', 4u),
          "'t' with three params still stalls");
    check(!coinft_v48_command_stalls_main_loop('s', 1u), "'s' never stalls");
    check(!coinft_v48_command_stalls_main_loop('i', 1u), "'i' never stalls");
}

static void test_set_channel_args(void)
{
    coinft_v48_set_channel_args_t ok = {3u, 0x34u, 0x12u, 0u};
    coinft_v48_set_channel_args_t rejected = {0xFFu, 0u, 0u, 0u};
    coinft_v48_set_channel_args_t edge = {11u, 0xFFu, 0xFFu, 0u};
    coinft_v48_set_channel_args_t past_end = {12u, 0u, 0u, 0u};

    check(coinft_v48_set_channel_applies(&ok), "index 3 applies");
    check(coinft_v48_set_channel_applies(&edge), "index 11 applies");
    check(!coinft_v48_set_channel_applies(&past_end),
          "index 12 is rejected: record 12 is not a UART channel");
    check(!coinft_v48_set_channel_applies(&rejected),
          "index 0xff is the safe no-op used to unstall the firmware");
    check(!coinft_v48_set_channel_applies(NULL), "null args rejected");

    check(coinft_v48_set_channel_value(&ok) == 0x1234u,
          "parameters are little-endian");
    check(coinft_v48_set_channel_value(&edge) == 0xFFFFu, "full-scale value");
    check(coinft_v48_set_channel_value(NULL) == 0u, "null yields zero");
}

static void test_activity_separation(void)
{
    /* Observed states, in order, during the v48 recovery sequence. */
    coinft_v48_activity_t scanning = {0x01001000u, true};
    coinft_v48_activity_t stalled = {0x00000000u, true};
    coinft_v48_activity_t idle = {0x00000000u, false};

    check(coinft_v48_scanning(&scanning), "INTR != 0 means scanning");
    check(!coinft_v48_scanning(&stalled), "INTR == 0 means not scanning");

    /* The trap: bytes keep flowing while the data behind them is frozen. */
    check(coinft_v48_transmitting_stale_data(&stalled),
          "stalled firmware still transmits stale records");
    check(!coinft_v48_transmitting_stale_data(&scanning),
          "a scanning device is not sending stale data");
    check(!coinft_v48_transmitting_stale_data(&idle),
          "idle with no traffic is not the stale-data case");
    check(!coinft_v48_scanning(NULL) &&
          !coinft_v48_transmitting_stale_data(NULL), "null rejected");
}

int main(void)
{
    test_command_lengths();
    test_stall_predicate();
    test_set_channel_args();
    test_activity_separation();

    if (g_failures != 0u) {
        printf("test_v48_command_protocol: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v48_command_protocol: passed\n");
    return 0;
}
