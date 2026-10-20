/* Host-only regression test for the recovered PERI divider helpers. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v39.c"

static void test_type1_integer_divider(void)
{
    uint8_t common[0x40u] = {0u};
    coinft_v39_common_divider_config_t config;
    coinft_v39_peri_shadow_t shadow;

    memset(&shadow, 0, sizeof(shadow));
    common[COINFT_V38_COMMON_DIVIDER_TYPE] = 1u;
    common[COINFT_V38_COMMON_DIVIDER_INDEX] = 0u;
    common[COINFT_V39_COMMON_DIVIDER_VALUE] = 0x55u;
    shadow.div_16_ctl[0] = 0x123456ffu;

    assert(coinft_v39_decode_common_divider_config(
               common, sizeof(common), &config));
    assert(config.raw_divider_type == 1u);
    assert(config.effective_divider_type == 1u);
    assert(config.divider_index == 0u);
    assert(config.requested_divider == 0x55u);
    assert(coinft_v39_apply_common_divider_from_config(
        &shadow, common, sizeof(common)));
    assert(shadow.write_count == 3u);
    assert(shadow.write_offsets[0] == COINFT_V39_PERI_DIV_CMD_OFFSET);
    assert(shadow.write_values[0] == 0x40000040u);
    assert(shadow.write_offsets[1] == COINFT_V39_PERI_DIV_16_OFFSET);
    assert(shadow.write_values[1] == ((0x54u << 8) |
                                      (0x123456ffu & COINFT_V39_PERI_INT_MASK)));
    assert(shadow.write_offsets[2] == COINFT_V39_PERI_DIV_CMD_OFFSET);
    assert(shadow.write_values[2] == 0x8000ff40u);
    assert(shadow.div_16_ctl[0] == shadow.write_values[1]);
    assert(shadow.div_cmd == 0x8000ff40u);

    /* A requested value of zero is normalized to one before encoding. */
    memset(&shadow, 0, sizeof(shadow));
    common[COINFT_V38_COMMON_DIVIDER_INDEX] = 3u;
    common[COINFT_V39_COMMON_DIVIDER_VALUE] = 0u;
    assert(coinft_v39_apply_common_divider_from_config(
        &shadow, common, sizeof(common)));
    assert(shadow.div_16_ctl[3] == 0u);
    assert(shadow.div_cmd == 0x8000ff43u);
}

static void test_type2_fractional_divider(void)
{
    uint8_t common[0x40u] = {0u};
    coinft_v39_peri_shadow_t shadow;
    uint32_t initial = 0xa5a5a5a5u;

    memset(&shadow, 0, sizeof(shadow));
    common[COINFT_V38_COMMON_DIVIDER_TYPE] = 2u;
    common[COINFT_V38_COMMON_DIVIDER_INDEX] = 4u;
    common[COINFT_V39_COMMON_DIVIDER_VALUE] = 10u;
    shadow.div_16_5_ctl[4] = initial;

    assert(coinft_v39_apply_common_divider_from_config(
        &shadow, common, sizeof(common)));
    assert(shadow.write_count == 4u);
    assert(shadow.write_offsets[0] == COINFT_V39_PERI_DIV_CMD_OFFSET);
    assert(shadow.write_values[0] == 0x40000084u);
    assert(shadow.write_offsets[1] == COINFT_V39_PERI_DIV_16_5_OFFSET + 16u);
    assert(shadow.write_values[1] == ((9u << 8) | (initial & 0xff0000ffu)));
    assert(shadow.write_offsets[2] == shadow.write_offsets[1]);
    assert(shadow.write_values[2] == (shadow.write_values[1] & 0xffffff07u));
    assert(shadow.write_offsets[3] == COINFT_V39_PERI_DIV_CMD_OFFSET);
    assert(shadow.write_values[3] == 0x8000ff84u);
    assert(shadow.div_16_5_ctl[4] == shadow.write_values[2]);
}

static void test_type3_fractional_divider_and_clamp(void)
{
    uint8_t common[0x40u] = {0u};
    coinft_v39_peri_shadow_t shadow;

    memset(&shadow, 0, sizeof(shadow));
    /* FUN_00001af4 clamps every type above two to type three. */
    common[COINFT_V38_COMMON_DIVIDER_TYPE] = 0xffu;
    common[COINFT_V38_COMMON_DIVIDER_INDEX] = 0u;
    common[COINFT_V39_COMMON_DIVIDER_VALUE] = 1u;
    shadow.div_24_5_ctl[0] = 0xabcdef7fu;

    assert(coinft_v39_apply_common_divider_from_config(
        &shadow, common, sizeof(common)));
    assert(shadow.write_count == 4u);
    assert(shadow.write_values[0] == 0x400000c0u);
    assert(shadow.write_offsets[1] == COINFT_V39_PERI_DIV_24_5_OFFSET);
    assert(shadow.write_values[1] == 0x0000007fu);
    assert(shadow.write_values[2] == 0x00000007u);
    assert(shadow.write_values[3] == 0x8000ffc0u);
    assert(shadow.div_24_5_ctl[0] == 0x00000007u);
}

static void test_validation_boundaries(void)
{
    coinft_v39_peri_shadow_t shadow;

    memset(&shadow, 0, sizeof(shadow));
    assert(coinft_v39_peri_set_divider16(
               &shadow, 1u, 14u, 0xffffu) == COINFT_V39_STATUS_OK);
    assert(coinft_v39_peri_set_divider16(
               &shadow, 1u, 15u, 0u) == COINFT_V39_STATUS_BAD_PARAM);
    assert(coinft_v39_peri_set_divider16(
               &shadow, 1u, 0u, 0x10000u) == COINFT_V39_STATUS_BAD_PARAM);
    assert(coinft_v39_peri_set_fractional_divider(
               &shadow, 2u, 5u, 0u, 0u) == COINFT_V39_STATUS_BAD_PARAM);
    assert(coinft_v39_peri_set_fractional_divider(
               &shadow, 2u, 0u, 0xffffu, 31u) == COINFT_V39_STATUS_OK);
    assert(coinft_v39_peri_set_fractional_divider(
               &shadow, 3u, 1u, 0u, 0u) == COINFT_V39_STATUS_BAD_PARAM);
    assert(coinft_v39_peri_disable_divider(
               &shadow, 0u, 0u) == COINFT_V39_STATUS_BAD_PARAM);

    /* Type zero follows the machine code's FUN_00003f18 path and fails
     * without producing any register writes. */
    {
        uint8_t common[0x40u] = {0u};
        common[COINFT_V38_COMMON_DIVIDER_TYPE] = 0u;
        common[COINFT_V38_COMMON_DIVIDER_INDEX] = 0u;
        common[COINFT_V39_COMMON_DIVIDER_VALUE] = 1u;
        memset(&shadow, 0, sizeof(shadow));
        assert(!coinft_v39_apply_common_divider(
            &shadow, common, sizeof(common), 1u));
        assert(shadow.write_count == 0u);
    }
}

int main(void)
{
    test_type1_integer_divider();
    test_type2_fractional_divider();
    test_type3_fractional_divider_and_clamp();
    test_validation_boundaries();
    return 0;
}
