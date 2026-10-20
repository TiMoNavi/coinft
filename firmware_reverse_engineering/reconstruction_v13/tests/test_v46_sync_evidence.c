/* Host-only checks for the v46 same-board synchronized evidence model.
 *
 * Every value below is taken from the 2026-08-30 KitProg3 capture in
 * 07_analysis/runtime_snapshots_v46_20260830/.  Nothing here touches MMIO. */

#include "../../07_analysis/coinft_firmware_pseudocode_v46.c"

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

/* Round 2 of sync_stream.json, read at 0x2000045c. */
static const coinft_v46_sensor_record_t k_stream_records[13] = {
    { 19777, 20042, 0, 0, 0x1E00 }, { 20477, 20688, 0, 0, 0x1E00 },
    { 24246, 24436, 0, 0, 0x1E00 }, { 21286, 21538, 0, 0, 0x1E00 },
    { 23037, 23264, 0, 0, 0x1E00 }, { 25416, 25596, 0, 0, 0x1E00 },
    { 16616, 16844, 0, 0, 0x1E00 }, { 16820, 16986, 0, 0, 0x1E00 },
    { 19257, 19462, 0, 0, 0x3C00 }, { 23753, 23935, 0, 0, 0x3C00 },
    { 25071, 25240, 0, 0, 0x3C00 }, { 15892, 16062, 0, 0, 0x3C00 },
    {  2048,  2048, 0, 0, 0x2000 },
};

/* The UART frame captured 1.6 ms after that read. */
static const uint16_t k_uart_frame[12] = {
    19777, 20477, 24246, 21286, 23037, 25416,
    16616, 16820, 19248, 23753, 25073, 15896,
};

/* Idle capture: baseline equals raw on every channel. */
static const coinft_v46_sensor_record_t k_idle_records[13] = {
    { 19829, 19829, 0, 0, 0x1E00 }, { 20525, 20525, 0, 0, 0x1E00 },
    { 24292, 24292, 0, 0, 0x1E00 }, { 21345, 21345, 0, 0, 0x1E00 },
    { 23089, 23089, 0, 0, 0x1E00 }, { 25455, 25455, 0, 0, 0x1E00 },
    { 16668, 16668, 0, 0, 0x1E00 }, { 16854, 16854, 0, 0, 0x1E00 },
    { 19295, 19295, 0, 0, 0x3C00 }, { 23787, 23787, 0, 0, 0x3C00 },
    { 25109, 25109, 0, 0, 0x3C00 }, { 15927, 15927, 0, 0, 0x3C00 },
    {  2048,  2048, 0, 0, 0x2000 },
};

static void test_record_layout(void)
{
    check(sizeof(coinft_v46_sensor_record_t) == 10u, "record is 10 bytes");
    /* The record array starts exactly where the 13 widget contexts end. */
    check(COINFT_V46_SENSOR_RECORD_BASE == 0x20000000 + 0x184 + 13 * 0x38,
          "record base abuts the widget context array");
    check(coinft_v46_sensor_record_addr(0) == 0x2000045Cu, "record 0 address");
    check(coinft_v46_sensor_record_addr(12) == 0x200004D4u, "record 12 address");
    /* 13 records of 10 bytes end at 0x200004de. */
    check(coinft_v46_sensor_record_addr(13) == 0x200004DEu, "record array end");
    check(coinft_v46_history_addr(0) == 0x2000091Au, "history base");
    check(coinft_v46_history_addr(12) == 0x20000992u, "history entry 12");
}

static void test_uart_mapping(void)
{
    uint16_t payload[12];
    unsigned exact = 0u;
    unsigned i;

    check(coinft_v46_uart_payload_from_records(k_stream_records, 13,
                                               payload, 12),
          "payload extraction succeeds");
    /* Record 12 must never reach the wire. */
    for (i = 0u; i < 12u; ++i) {
        if (payload[i] == k_uart_frame[i])
            ++exact;
    }
    check(exact == 9u, "9 of 12 channels match the nearest UART frame exactly");
    check(payload[0] == 19777u && payload[11] == 15892u, "channel order");

    /* Wrong sizes are rejected rather than silently truncated. */
    check(!coinft_v46_uart_payload_from_records(k_stream_records, 13,
                                                payload, 13),
          "13-channel payload rejected");
    check(!coinft_v46_uart_payload_from_records(k_stream_records, 5,
                                                payload, 12),
          "short record array rejected");
}

static void test_baseline_invariant(void)
{
    check(coinft_v46_idle_baseline_invariant(k_idle_records, 13),
          "idle: baseline == raw on all 13 records");
    check(!coinft_v46_idle_baseline_invariant(k_stream_records, 13),
          "stream: baseline diverges from raw");
    /* Record 12 satisfies the invariant in both states. */
    check(coinft_v46_idle_baseline_invariant(&k_stream_records[12], 1),
          "record 12 baseline == raw while streaming");
}

static void test_limit_groups(void)
{
    unsigned i;
    for (i = 0u; i < 8u; ++i)
        check(k_stream_records[i].limit_word == COINFT_V46_LIMIT_GROUP_LOW,
              "channels 0..7 share the low limit word");
    for (i = 8u; i < 12u; ++i)
        check(k_stream_records[i].limit_word == COINFT_V46_LIMIT_GROUP_HIGH,
              "channels 8..11 share the high limit word");
    check(k_stream_records[12].limit_word == COINFT_V46_LIMIT_INDEX12,
          "record 12 has its own limit word");
}

static void test_frame_registers(void)
{
    uint32_t offset = 0u;
    check(coinft_v46_frame_register_offset(0, &offset) && offset == 0x20Cu,
          "frame word 0 -> SNS_SW_SEL_CSW_MASK2");
    check(coinft_v46_frame_register_offset(3, &offset) && offset == 0x218u,
          "frame word 3 -> SNS_SCAN_CTL");
    check(coinft_v46_frame_register_offset(5, &offset) && offset == 0x220u,
          "frame word 5 -> SNS_CTL");
    check(!coinft_v46_frame_register_offset(6, &offset),
          "frame word 6 rejected");
    check(!coinft_v46_frame_register_offset(0, NULL), "null offset rejected");
}

static void test_interrupt_model(void)
{
    /* Observed on the live target. */
    const uint32_t mask = 0x00000100u;
    check(coinft_v46_scan_interrupt_enabled(mask),
          "only SCAN is unmasked");
    check(!coinft_v46_scan_interrupt_enabled(mask | COINFT_V46_INTR_INIT),
          "extra unmasked cause breaks the invariant");
    check(!coinft_v46_scan_interrupt_enabled(0u), "empty mask rejected");

    /* INTR values actually read while streaming. */
    check((0x00001000u & COINFT_V46_INTR_INIT) != 0u, "0x1000 is INIT");
    check((0x01001000u & COINFT_V46_INTR_CONFIG_REQ) != 0u,
          "0x01001000 carries CONFIG_REQ");
    check((0x80001000u & COINFT_V46_INTR_FIFO_OVERFLOW) != 0u,
          "0x80001000 carries FIFO_OVERFLOW");
    /* None of those causes are unmasked, so they do not raise the IRQ. */
    check(!coinft_v46_intr_pending(0x01001000u, mask, COINFT_V46_INTR_CONFIG_REQ),
          "CONFIG_REQ is masked off");
    check(coinft_v46_intr_pending(0x00000100u, mask, COINFT_V46_INTR_SCAN),
          "SCAN passes the mask");

    check(coinft_v46_frame_cmd_start() == 1u, "FRAME_CMD.START_FRAME == 1");
}

static void test_tearing_classifier(void)
{
    uint16_t sram[12];
    uint16_t uart[12];
    unsigned i;
    for (i = 0u; i < 12u; ++i) {
        sram[i] = (uint16_t)(1000u + i);
        uart[i] = sram[i];
    }
    check(coinft_v46_mismatch_is_contiguous_run(sram, uart, 12),
          "identical arrays are a zero-run match");

    sram[3] = 7u;
    sram[4] = 8u;
    check(coinft_v46_mismatch_is_contiguous_run(sram, uart, 12),
          "one contiguous run is accepted as tearing");

    sram[9] = 9u;
    check(!coinft_v46_mismatch_is_contiguous_run(sram, uart, 12),
          "two separate runs are not simple tearing");
    check(!coinft_v46_mismatch_is_contiguous_run(NULL, uart, 12),
          "null input rejected");
}

int main(void)
{
    test_record_layout();
    test_uart_mapping();
    test_baseline_invariant();
    test_limit_groups();
    test_frame_registers();
    test_interrupt_model();
    test_tearing_classifier();

    if (g_failures != 0u) {
        printf("test_v46_sync_evidence: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v46_sync_evidence: passed\n");
    return 0;
}
