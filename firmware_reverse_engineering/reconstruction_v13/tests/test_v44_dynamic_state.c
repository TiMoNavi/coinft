/* Host-only regression test for the recovered v44 dynamic widget state. */
#include <assert.h>
#include <stdint.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v44.c"

static coinft_v44_dynamic_environment_t default_environment(void)
{
    coinft_v44_dynamic_environment_t environment = {
        .widget_count = 1u,
        .period_bias = 3u,
        .period_shift = 0,
        .scale_word = 0x084cu,
        .final_sub_conversions = 0x002cu
    };
    return environment;
}

int main(void)
{
    coinft_v44_dynamic_environment_t environment = default_environment();
    coinft_v44_dynamic_config_t config = { .method = 1u,
                                           .compare_selector = 2u };
    coinft_v44_dynamic_widget_t widget = { .sense_clk = 16u,
                                           .sub_conversions = 0u,
                                           .noise_percent = 100u,
                                           .mode = 4u,
                                           .tuning = 0u };
    coinft_v44_dynamic_result_t result;

    /* FUN_00001bd8 scans bits 15..1 and returns the shifted 0xffff seed. */
    assert(coinft_v44_msb_scale(0x8000u, 0xffffu) == 0xffffu);
    assert(coinft_v44_msb_scale(0x4000u, 0xffffu) == 0x7fffu);
    assert(coinft_v44_msb_scale(0x0002u, 0xffffu) == 3u);
    assert(coinft_v44_msb_scale(0x0001u, 0xffffu) == 1u);
    assert(coinft_v44_msb_scale(0x0000u, 0xffffu) == 1u);

    /* Shift amounts of 32 and 255 are zero on a 32-bit ARM register. */
    assert(coinft_v44_arm_lsl1(31u) == UINT32_C(0x80000000));
    assert(coinft_v44_arm_lsl1(32u) == 0u);
    assert(coinft_v44_arm_lsl1(255u) == 0u);
    assert(coinft_v44_arm_lsr_u32(UINT32_C(0x80000000), 31u) == 1u);
    assert(coinft_v44_arm_lsr_u32(UINT32_C(0xffffffff), 32u) == 0u);

    /* FUN_00001cb8/FUN_00001cec tuning buckets. */
    assert(coinft_v44_compute_tuning(&widget, &config, 0) == 0x82u);
    widget.noise_percent = 10u;
    assert(coinft_v44_compute_tuning(&widget, &config, 0) == 0x80u);
    widget.noise_percent = 200u;
    /* The original call site passes this byte as a signed char. */
    assert(coinft_v44_compute_tuning(&widget, &config, 0) == 0x82u);
    widget.noise_percent = 100u;

    /* FUN_00001bfc: ordinary captured scale remains in mode 0x08. */
    widget.sub_conversions = 0u;
    assert(coinft_v44_compute_mode8(&widget, &environment) == 0x08u);
    widget.sub_conversions = 0x0400u;
    assert(coinft_v44_compute_mode8(&widget, &environment) == 0x0au);
    widget.sub_conversions = 0u;

    /* Selector 2 ignores the scale-vs-sum comparison. */
    environment.scale_word = 0xffffu;
    assert(coinft_v44_compute_dynamic_mode(&widget, &config,
                                           &environment, true) == 0x05u);
    config.compare_selector = 1u;
    assert(coinft_v44_compute_dynamic_mode(&widget, &config,
                                           &environment, true) == 0x04u);
    config.compare_selector = 0u;
    assert(coinft_v44_compute_dynamic_mode(&widget, &config,
                                           &environment, false) == 0x04u);
    widget.sub_conversions = 0xffffu;
    assert(coinft_v44_compute_dynamic_mode(&widget, &config,
                                           &environment, true) == 0x05u);
    config.compare_selector = 2u;
    environment.scale_word = 0x084cu;
    widget.sub_conversions = 0u;

    /* FUN_00001d18 mode 4 update computes mode 5 and is valid at sense=16. */
    widget.mode = 4u;
    widget.tuning = 0u;
    assert(coinft_v44_update_dynamic_widget(&widget, &config,
                                             &environment, true) == 0u);
    assert(widget.mode == 0x05u);

    /* A mode-1 widget adds the LFSR period guard; sense=9 is too short. */
    widget.sense_clk = 9u;
    widget.mode = 1u;
    widget.tuning = 0u;
    assert(coinft_v44_update_dynamic_widget(&widget, &config,
                                             &environment, true) == 0x800u);
    widget.sense_clk = 16u;

    /* With an ARM shift count of 32, the added period is zero, not one. */
    environment.period_shift = 31;
    widget.mode = 1u;
    widget.tuning = 0u;
    widget.sense_clk = 8u;
    assert(coinft_v44_update_dynamic_widget(&widget, &config,
                                             &environment, true) == 0u);
    environment.period_shift = 0;
    widget.sense_clk = 16u;

    /* One ordinary mode-4 widget leaves the common terminal period at 3. */
    widget.mode = 4u;
    widget.tuning = 0u;
    assert(coinft_v44_update_dynamic_widgets(&widget, &config, 1u,
                                              &environment, true, &result));
    assert(result.status == 0u);
    assert(result.updated_widgets == 1u);
    assert(result.terminal_period == 3u);
    assert(result.terminal_sub_conversions == 0x002cu);

    /* Mode 8 can produce 0x0a (bit 1 set), forcing both 0x3ff outputs. */
    environment.scale_word = 0x8000u;
    widget.sub_conversions = 0x4000u;
    widget.mode = 8u;
    widget.sense_clk = 16u;
    assert(coinft_v44_update_dynamic_widgets(&widget, &config, 1u,
                                              &environment, true, &result));
    assert(widget.mode == 0x0au);
    assert(result.terminal_period == 0x3ffu);
    assert(result.terminal_sub_conversions == 0x3ffu);

    /* Sense clocks above 0x1000 return the firmware's 0x800 error. */
    environment.scale_word = 0x084cu;
    widget.sub_conversions = 0u;
    widget.mode = 4u;
    widget.sense_clk = 0x1001u;
    assert(coinft_v44_update_dynamic_widget(&widget, &config,
                                             &environment, true) == 0x800u);
    return 0;
}
