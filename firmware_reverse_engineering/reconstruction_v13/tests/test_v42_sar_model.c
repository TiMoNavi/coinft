/* Host-only regression test for the recovered SAR0 configuration/result path. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v42.c"

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void test_common_sar_registers(void)
{
    uint8_t config[0x7cu] = {0u};
    coinft_v42_sar_shadow_t shadow;
    memset(&shadow, 0, sizeof(shadow));
    config[0] = 3u;
    config[1] = 1u;
    config[2] = 7u;
    config[3] = 1u;
    config[4] = 1u;
    config[5] = 2u;
    config[6] = 1u;
    config[7] = 1u;
    config[8] = 1u;
    config[9] = 2u;
    config[10] = 1u;
    config[11] = 3u;
    config[12] = 4u;
    config[13] = 5u;
    config[14] = 2u;
    config[15] = 1u;
    put_u16(config + 0x10u, 0x0123u);
    put_u16(config + 0x14u, 0x0234u);
    put_u16(config + 0x18u, 0x0345u);
    put_u16(config + 0x1cu, 0x0456u);
    put_u16(config + 0x20u, 0x1111u);
    put_u16(config + 0x24u, 0x2222u);
    config[0x28u] = 1u;
    put_u16(config + 0x2cu, 0x0003u);
    assert(coinft_v42_sar_pack_common(&shadow, config, sizeof(config)));
    assert((shadow.ctrl & 0x30000000u) == 0x20000000u);
    assert((shadow.ctrl & 0x70u) == 0x30u);
    assert((shadow.sample_ctrl & 0x7u) == 0x5u);
    assert((shadow.sample_time01 & 0x3ffu) == 0x123u);
    assert(((shadow.sample_time01 >> 16u) & 0x3ffu) == 0x234u);
    assert((shadow.range_thres & 0xffffu) == 0x1111u);
    assert((shadow.range_thres >> 16u) == 0x2222u);
    assert(shadow.range_cond == 0x40000000u);
    assert(shadow.chan_en == 3u);
    assert(shadow.pump_ctrl == 0x80000000u);
    assert(shadow.mux_switch_clear0 == COINFT_V42_SAR_DEFAULT_MUX_CLEAR);
}

static void test_sparse_channels_and_result(void)
{
    uint8_t config[0x7cu] = {0u};
    coinft_v42_sar_shadow_t shadow;
    coinft_v42_sar_channel_record_t records[17];
    uint32_t mux_words[2] = {0x12u, 0x34u};
    uint16_t result = 0u;
    memset(&shadow, 0, sizeof(shadow));
    memset(records, 0, sizeof(records));
    config[2] = 7u;
    config[10] = 1u;
    records[0].present = true;
    records[0].bytes[0] = 0x25u;
    records[0].bytes[1] = 0u;
    records[0].bytes[2] = 1u;
    records[0].bytes[3] = 0u;
    records[0].bytes[4] = 2u;
    records[0].bytes[5] = 1u;
    records[0].bytes[6] = 0u;
    records[16].present = true;
    records[16].bytes[0] = 0x01u;
    records[16].bytes[1] = 2u;
    records[16].bytes[2] = 0u;
    records[16].bytes[3] = 1u;
    records[16].bytes[4] = 0u;
    records[16].bytes[5] = 2u;
    records[16].bytes[6] = 3u;
    assert(coinft_v42_configure_sar(&shadow, config, sizeof(config),
                                    records, 17u, 0x12345678u, mux_words));
    assert(shadow.chan_config[0] == (0x25u | (1u << 9u) | (2u << 12u)));
    assert(shadow.channel_offset[0] == (int16_t)0xfffff800u);
    assert((shadow.range_intr & 1u) != 0u);
    assert(shadow.chan16_config != 0u);
    assert(shadow.intr == ((2u << 6u) | (3u << 5u)));
    assert(shadow.intr_mask == shadow.intr);
    assert(shadow.mux_switch0 == 0x12u && shadow.mux_switch_hw_ctrl == 0x34u);
    shadow.chan_result[0] = 0xabcd;
    assert(coinft_v42_read_and_store_sar_result(&shadow, 0u, &result));
    assert(result == 0xabcdu && shadow.intr == 1u);
    assert(!coinft_v42_read_and_store_sar_result(&shadow, 16u, &result));
}

int main(void)
{
    test_common_sar_registers();
    test_sparse_channels_and_result();
    return 0;
}
