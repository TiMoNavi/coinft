/* Host-only regression test for v38 SNS_CTL source separation. */
#include <assert.h>
#include <stdint.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v38.c"

int main(void)
{
    uint8_t context[0x38u] = {0u};
    uint8_t shared[0x50u] = {0u};
    uint8_t common[0x4cu] = {0u};
    coinft_v38_tuning_view_t tuning;
    uint32_t base = 0x20030003u;

    coinft_v38_common_divider_view_t divider;
    context[COINFT_V38_WIDGET_LFSR_FIELD] = 0x80u;
    assert(coinft_v38_decode_tuning(context, sizeof(context), &tuning));
    assert(tuning.sense_tuning_word == 0x0080u);
    assert(tuning.lfsr_bits == 0u);
    assert(tuning.tuning_high_byte == 0u);

    common[COINFT_V38_COMMON_DIVIDER_TYPE] = 1u;
    common[COINFT_V38_COMMON_DIVIDER_INDEX] = 0u;
    assert(coinft_v38_decode_common_divider(common, sizeof(common), &divider));
    assert(divider.divider_type == 1u && divider.divider_index == 0u);

    /* Observed CoinFT case: method=1, selector=0 selects shared +0x49. */
    shared[COINFT_V38_SHARED_SNS_MODE_BASE] = 0u;
    common[COINFT_V38_COMMON_SNS_MODE_OFFSET] = 2u;
    assert(coinft_v38_compose_sns_ctl_modes(
               base, 1u, 0u, shared, sizeof(shared), common, sizeof(common))
           == 0x20030023u);

    /* Alternate selector selects method+1, represented here by shared +0x4b. */
    shared[COINFT_V38_SHARED_SNS_MODE_BASE + 2u] = 1u;
    assert(coinft_v38_compose_sns_ctl_modes(
               base, 1u, 1u, shared, sizeof(shared), common, sizeof(common))
           == 0x20030063u);

    /* A zero method must not underflow the method-1 table index. */
    shared[COINFT_V38_SHARED_SNS_MODE_BASE] = 3u;
    assert(coinft_v38_compose_sns_ctl_modes(
               base, 0u, 0u, shared, sizeof(shared), common, sizeof(common))
           == 0x200300e3u);
    return 0;
}
