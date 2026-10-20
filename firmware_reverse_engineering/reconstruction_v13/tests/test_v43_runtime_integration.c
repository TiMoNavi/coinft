/* Host-only integration test for the recovered v43 lifecycle boundaries. */
#include <assert.h>
#include <stdint.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v43.c"

int main(void)
{
    coinft_v43_runtime_state_t state;
    assert(coinft_v43_begin_runtime(&state));
    assert(state.clock.gpio_init_complete == 1u);
    assert(state.public_pins_reset == 1u);
    assert(state.shield_pins_reset == 1u);
    assert(state.event_count == 3u);
    assert(state.events[0] == COINFT_V43_EVENT_RESET);
    assert(state.events[1] == COINFT_V43_EVENT_CLOCKS_READY);
    assert(state.events[2] == COINFT_V43_EVENT_PINS_RESET);

    assert(coinft_v43_mark_sar_configured(&state));
    assert(coinft_v43_csd_scan_start(&state));
    assert(!coinft_v43_csd_scan_start(&state));
    assert(coinft_v43_csd_scan_complete(&state));
    state.uart_streaming = 1u;
    assert(coinft_v43_uart_frame_ready(&state));
    assert(coinft_v43_store_sar_result(&state, 0x1234u));
    assert(state.sar_result_valid == 1u);
    assert(state.sar_result_sram == 0x1234u);
    assert(state.event_count == 8u);
    assert(state.events[3] == COINFT_V43_EVENT_SAR_CONFIGURED);
    assert(state.events[4] == COINFT_V43_EVENT_CSD_SCAN_STARTED);
    assert(state.events[5] == COINFT_V43_EVENT_CSD_SCAN_COMPLETE);
    assert(state.events[6] == COINFT_V43_EVENT_UART_FRAME_READY);
    assert(state.events[7] == COINFT_V43_EVENT_SAR_RESULT_STORED);
    return 0;
}
