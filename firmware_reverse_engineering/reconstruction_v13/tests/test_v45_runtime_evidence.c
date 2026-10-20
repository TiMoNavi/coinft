/* Host-only regression test for the recovered v44 dynamic widget state. */
#include <assert.h>
#include <stdint.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v45.c"

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

    /* v45: the synchronized UART capture contains complete 26-byte frames. */
    {
        static const uint8_t bytes[COINFT_V45_UART_FRAME_SIZE] = {
            0x02u, 0x8eu, 0x4cu, 0x67u, 0x4fu, 0x4cu, 0x5eu,
            0x87u, 0x52u, 0x5eu, 0x59u, 0xb2u, 0x62u, 0x4fu,
            0x40u, 0x34u, 0x41u, 0x9bu, 0x4au, 0x4au, 0x5cu,
            0x69u, 0x61u, 0x94u, 0x3du, 0x03u
        };
        coinft_v45_uart_frame_t frame;
        assert(coinft_v45_decode_uart_frame(bytes, sizeof(bytes), &frame));
        assert(frame.values[0] == 0x4c8eu);
        assert(frame.values[11] == 0x3d94u);
        assert(!coinft_v45_decode_uart_frame(bytes, sizeof(bytes) - 1u,
                                             &frame));
    }

    /* +0x04 is the runtime LAST bit: it is set only for an end slot. */
    assert(coinft_v45_set_last_bit(0x20030023u, false) == 0x20030023u);
    assert(coinft_v45_set_last_bit(0x20030023u, true) == 0x20030027u);
    assert(coinft_v45_set_last_bit(0x20030027u, false) == 0x20030023u);

    /* A failed read must not be accepted as a complete SRAM snapshot. */
    assert(coinft_v45_sram_dump_usable(COINFT_V45_FULL_SRAM_SIZE, false));
    assert(!coinft_v45_sram_dump_usable(COINFT_V45_FULL_SRAM_SIZE, true));
    assert(!coinft_v45_sram_dump_usable(COINFT_V45_STREAM_SRAM_SIZE, false));

    {
        coinft_v45_msc_observation_t first = {
            .status_word = 0x00001000u,
            .runtime_flag = 0u,
            .sensor_frame = {0x7eu, 0x6eu, 0x10u, 0x000303ffu,
                             0x00401e14u, 0x20030023u},
            .sram_dump_complete = true
        };
        coinft_v45_msc_observation_t changed = first;
        changed.sensor_frame[5] = 0x20030027u;
        assert(coinft_v45_observed_scan_flag(&first));
        assert(coinft_v45_frame_state_equal(&first, &first));
        assert(!coinft_v45_frame_state_equal(&first, &changed));
    }
    return 0;
}
