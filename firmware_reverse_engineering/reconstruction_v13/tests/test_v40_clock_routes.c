/* Host-only regression test for S593 PCLK routing and startup ordering. */
#include <assert.h>
#include <stdint.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v40.c"

int main(void)
{
    uint32_t values[5] = {0u};
    coinft_v40_pclk_route_t route;
    coinft_v40_startup_clock_state_t state;

    assert(coinft_v40_compose_pclk_route(
               COINFT_V40_PCLK_SCB3, 1u, 2u,
               COINFT_V40_PCLK_ROLE_SCB3, &values[0], &route));
    assert(values[0] == 0x42u);
    assert(route.pclk == 3u && route.divider_index == 2u);
    assert(route.role == COINFT_V40_PCLK_ROLE_SCB3);

    assert(coinft_v40_route_boot_pclks(values, 5u) == 5u);
    assert(values[0] == 0x40u); /* MSC0 -> DIV_16_CTL[0] */
    assert(values[1] == 0x40u); /* MSC1 -> DIV_16_CTL[0] */
    assert(values[2] == 0x43u); /* SAR -> DIV_16_CTL[3] */
    assert(values[3] == 0x42u); /* SCB3 -> DIV_16_CTL[2] */
    assert(values[4] == 0x41u); /* TCPWM0 -> DIV_16_CTL[1] */
    assert(coinft_v40_route_boot_pclks(values, 2u) == 2u);

    coinft_v40_startup_clock_state(&state);
    assert(state.divider_boot_count == 4u);
    assert(state.pclk_route_count == 5u);
    assert(state.msc_clock_ready == 1u);
    assert(state.scb3_clock_ready == 1u);
    assert(state.sar_clock_ready == 1u);
    assert(state.gpio_init_complete == 1u);
    assert(state.phase == 0x2du);
    return 0;
}
