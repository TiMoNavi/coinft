/* Host-only checks for v49 stage F: function identification.
 *
 * The assertions about FUN_00004378 / FUN_00004484 exist to stop the old wrong
 * labels from coming back: a test that says "this is integer division" is
 * harder to quietly lose than a sentence in a progress document. */

#include "../../07_analysis/coinft_firmware_pseudocode_v49.c"

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

static void test_runtime_helpers(void)
{
    /* Addresses, so a future round cannot re-label them silently. */
    check(COINFT_V49_HELPER_UIDIVMOD == 0x00004378,
          "FUN_00004378 is the division helper, not a clock conversion");
    check(COINFT_V49_HELPER_DIVMOD_GUARD == 0x00004484,
          "FUN_00004484 is the divmod guard, not a validity callback");
    check(COINFT_V49_HELPER_IDIV0 == 0x0000448C, "idiv0 hook address");
    check(COINFT_V49_HELPER_MEMSET == 0x000044B8, "memset address");

    check(coinft_v49_divmod_guard_divides(1u), "non-zero divisor divides");
    check(coinft_v49_divmod_guard_divides(0xFFFFFFFFu), "max divisor divides");
    check(!coinft_v49_divmod_guard_divides(0u),
          "zero divisor routes to the idiv0 hook");
}

static void test_widget_enable(void)
{
    /* 0x2fb is the firmware's own loop bound; the static assert in the
     * pseudocode already ties it to 13 widgets, this checks the walk. */
    uint8_t contexts[0x400];
    unsigned armed;
    memset(contexts, 0, sizeof contexts);

    armed = coinft_v49_enable_all_widgets(contexts,
                                          COINFT_V49_WIDGET_ENABLE_LOOP_END);
    check(armed == 13u, "the enable loop arms exactly 13 widgets");

    /* Every widget's +0x23 byte must now pass FUN_000027b0's test. */
    for (unsigned i = 0; i < 13u; ++i) {
        unsigned off = COINFT_V49_WIDGET_ENABLE_OFFSET + i * 0x38u;
        check(coinft_v49_widget_armed(contexts[off]),
              "each widget's enable byte satisfies (b & 6) == 6");
    }
    /* And nothing outside the stride was touched. */
    check(contexts[COINFT_V49_WIDGET_ENABLE_OFFSET + 1u] == 0u,
          "the loop writes only the +0x23 byte of each context");
    check(contexts[COINFT_V49_WIDGET_ENABLE_OFFSET + 13u * 0x38u] == 0u,
          "the loop stops at the 13th widget");

    /* The predicate is an exact mask test, not a non-zero test. */
    check(!coinft_v49_widget_armed(0x02u), "bit1 alone is not armed");
    check(!coinft_v49_widget_armed(0x04u), "bit2 alone is not armed");
    check(coinft_v49_widget_armed(0x06u), "bits 1|2 are armed");
    check(coinft_v49_widget_armed(0xFFu), "extra bits still count as armed");
    check(coinft_v49_enable_all_widgets(NULL, 0x2FBu) == 0u, "null rejected");
}

static void test_scb_uart(void)
{
    /* MODE == 2 selects UART; the firmware's config word carries 0x2000000. */
    check(coinft_v49_scb_configured_as_uart(0x02000000u),
          "0x2000000 is MODE=2=UART");
    check(coinft_v49_scb_ctrl_mode(0x02000000u) == 2u, "mode field extraction");
    check(!coinft_v49_scb_configured_as_uart(0x01000000u), "MODE=1 is not UART");
    check(!coinft_v49_scb_configured_as_uart(0x00000000u), "MODE=0 is not UART");
    /* Bits outside the mode field must not affect the decision. */
    check(coinft_v49_scb_configured_as_uart(0x02FF00FFu),
          "unrelated bits do not disturb the mode test");

    check(COINFT_V49_SCB_UART_CTRL == 0x40 &&
          COINFT_V49_SCB_UART_RX_CTRL == 0x48,
          "SCB UART register offsets match cyip_scb_v3.h");
}

static void test_tcpwm(void)
{
    /* CNT[0] starts at 0x100; FUN_00004274 computes (n+4)*0x40 for CTRL. */
    check(coinft_v49_tcpwm_reg(0u, COINFT_V49_TCPWM_CNT_CTRL) == 0x100u,
          "counter 0 CTRL at 0x100");
    check(coinft_v49_tcpwm_reg(0u, COINFT_V49_TCPWM_CNT_COUNTER) == 0x108u,
          "counter 0 COUNTER at 0x108");
    check(coinft_v49_tcpwm_reg(0u, COINFT_V49_TCPWM_CNT_PERIOD) == 0x114u,
          "counter 0 PERIOD at 0x114");
    check(coinft_v49_tcpwm_reg(0u, COINFT_V49_TCPWM_CNT_TR_CTRL0) == 0x120u,
          "counter 0 TR_CTRL0 at 0x120");
    check(coinft_v49_tcpwm_reg(1u, COINFT_V49_TCPWM_CNT_CTRL) == 0x140u,
          "counter 1 CTRL is one 0x40 stride further");
    /* (param_2 + 4) * 0x40 in the disassembly must equal our formula. */
    for (unsigned n = 0; n < 4u; ++n) {
        check(coinft_v49_tcpwm_reg(n, COINFT_V49_TCPWM_CNT_CTRL) ==
              (n + 4u) * 0x40u,
              "(n+4)*0x40 matches the CNT[n] CTRL address");
    }
}

int main(void)
{
    test_runtime_helpers();
    test_widget_enable();
    test_scb_uart();
    test_tcpwm();

    if (g_failures != 0u) {
        printf("test_v49_stage_f: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v49_stage_f: passed\n");
    return 0;
}
