/* Host-only regression test for the v36 recovered special branch. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v36.c"

static unsigned callback_count;
static uint8_t *callback_arg[2];

static void record_callback(uint8_t *context)
{
    if (callback_count < 2u)
        callback_arg[callback_count] = context;
    ++callback_count;
}

int main(void)
{
    uint8_t shared_context[0x44u] = {0u};
    uint8_t callback_context[0x40u] = {0u};
    uint32_t activity_word = 0u;
    uint32_t msc_runtime0[0x65u] = {0u};
    uint32_t msc_runtime1[0x65u] = {0u};
    volatile uint32_t sns_ctl0 = 0u;
    volatile uint32_t sns_ctl1 = 0u;
    uint16_t candidate_slot = 3u;
    uint32_t sns_source0 = 0x12340000u;
    uint32_t sns_source1 = 0x56780000u;
    coinft_v36_rearm_view_t view = {
        shared_context,
        &activity_word,
        {msc_runtime0, msc_runtime1},
        {&sns_ctl0, &sns_ctl1},
        callback_context,
        record_callback,
        false
    };

    shared_context[0x43u] = 2u;
    shared_context[0x40u] = 1u;
    callback_context[0x23u] = 6u;
    assert(coinft_v36_widget_context_enabled(callback_context,
                                             sizeof(callback_context)));
    memcpy(shared_context + 0x2cu, &candidate_slot,
           sizeof(candidate_slot));
    memcpy(shared_context + 0x20u, &sns_source0, sizeof(sns_source0));
    memcpy(shared_context + 0x24u, &sns_source1, sizeof(sns_source1));

    assert(coinft_v36_special_pair_rearm(&view, 3u, 1u));
    assert(msc_runtime0[0x190u / sizeof(uint32_t)] == 1u);
    assert(msc_runtime1[0x190u / sizeof(uint32_t)] == 1u);
    assert(activity_word == 0x83u);
    assert(callback_count == 2u);
    assert(callback_arg[0] == callback_context);
    assert(callback_arg[1] == callback_context + 0x18u);
    assert(sns_ctl0 == (sns_source0 | 5u));
    assert(sns_ctl1 == (sns_source1 | 5u));

    /* Reset is a callback sentinel only; it does not suppress hardware side effects. */
    activity_word = 0u;
    callback_count = 0u;
    sns_ctl0 = 0u;
    sns_ctl1 = 0u;
    msc_runtime0[0x190u / sizeof(uint32_t)] = 0u;
    msc_runtime1[0x190u / sizeof(uint32_t)] = 0u;
    view.callback_is_reset = true;
    assert(coinft_v36_special_pair_rearm(&view, 3u, 1u));
    assert(callback_count == 0u);
    assert(activity_word == 0x83u);
    assert(sns_ctl0 == (sns_source0 | 5u));
    assert(sns_ctl1 == (sns_source1 | 5u));
    view.callback_is_reset = false;

    /* Any failed predicate must leave all side effects untouched. */
    activity_word = 0x2000u;
    callback_count = 0u;
    sns_ctl0 = 0u;
    sns_ctl1 = 0u;
    msc_runtime0[0x190u / sizeof(uint32_t)] = 0u;
    msc_runtime1[0x190u / sizeof(uint32_t)] = 0u;
    assert(!coinft_v36_special_pair_rearm(&view, 3u, 1u));
    assert(callback_count == 0u);
    assert(sns_ctl0 == 0u && sns_ctl1 == 0u);
    assert(msc_runtime0[0x190u / sizeof(uint32_t)] == 0u);
    assert(msc_runtime1[0x190u / sizeof(uint32_t)] == 0u);
    return 0;
}
