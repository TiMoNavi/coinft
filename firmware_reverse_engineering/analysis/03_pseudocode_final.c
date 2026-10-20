/*
 * CoinFT V2 固件伪代码 - 最终交付版
 * 
 * 版本：v54（基于 v51 伪代码）
 * 来源：逆向工程 PSoC 4 固件（CY8C4149AZI-S593）
 * 验证：82249/82249 帧往返测试通过
 * 
 * 本文件是从 Ghidra 反汇编恢复的伪代码，已通过以下验证：
 * - 151/151 函数已识别
 * - MSC/GPIO/UART/PCLK/Em_EEPROM 寄存器位域级精确
 * - record/history/frame 数据结构字节级确认
 * - 6 个命令协议全闭合（含阻塞语义与 Flash 副作用）
 * - 行为等价：82249/82249 帧无筛选验证
 * 
 * 注意事项：
 * 1. 所有函数名（FUN_XXXXXXXX）和变量名都是推断的，不是原始名称
 * 2. field2/field3 在所有测试中恒为零，可能是预留字段
 * 3. SAR 已初始化但 chan_en=0，17 个通道未使用
 * 4. empty_stub 的 8 个函数无副作用，可忽略
 * 5. limit_word 与 cdac_sel_re 的精确关系未完全闭合，但不影响重建
 * 
 * 重建固件时建议：
 * - 使用本文件作为架构参考，不要逐字复制
 * - 优先使用 ModusToolbox CapSense 库，而非手动管理 MSC 寄存器
 * - 保持主循环状态机和命令协议的语义一致性
 * - 通过往返测试验证行为等价性
 * 
 * 详细文档见：
 * - 01_project_background.md
 * - 02_firmware_architecture.md
 * - 04_reconstruction_guide.md
 */

/* CoinFT reconstruction v51: full UART command table and baseline watch. */
#include "coinft_firmware_pseudocode_v33.c"
#include <stddef.h>

typedef struct {
    uint16_t finger_cap;
    uint16_t sig_pfc;
    uint16_t max_raw_count;
    uint16_t max_raw_count_row;
    uint16_t finger_th;
    uint16_t prox_th;
    uint16_t low_bsln_rst;
    uint16_t sns_clk;
    uint16_t row_sns_clk;
    uint32_t gesture_detected;
    uint32_t gesture_direction;
    int16_t x_delta;
    int16_t y_delta;
    uint16_t noise_th;
    uint16_t n_noise_th;
    uint16_t hysteresis;
    uint8_t on_debounce;
    uint8_t sns_clk_source;
    uint16_t num_sub_conversions;
    uint8_t cdac_ref;
    uint8_t row_cdac_ref;
    uint8_t cic_rate;
    uint8_t cdac_dither_en;
    uint8_t cdac_dither_value;
    uint8_t coarse_init_bypass_en;
    uint16_t cdac_comp_divider;
    uint8_t lfsr_bits;
    uint8_t reserved_padding;
    uint32_t reserved0;
} coinft_v34_context_candidate_t;

_Static_assert(sizeof(coinft_v34_context_candidate_t) == 0x38u,
               "CoinFT context candidate must be 0x38 bytes");
_Static_assert(offsetof(coinft_v34_context_candidate_t, max_raw_count) == 0x04u,
               "maxRawCount offset mismatch");
_Static_assert(offsetof(coinft_v34_context_candidate_t, sns_clk) == 0x0eu,
               "snsClk offset mismatch");
_Static_assert(offsetof(coinft_v34_context_candidate_t, num_sub_conversions) == 0x28u,
               "numSubConversions offset mismatch");
_Static_assert(offsetof(coinft_v34_context_candidate_t, cdac_comp_divider) == 0x30u,
               "cdacCompDivider offset mismatch");

enum {
    COINFT_V35_CTX_RAW_LIMIT = 0x04,
    COINFT_V35_CTX_SNS_CLK = 0x0e,
    COINFT_V35_CTX_ROW_SNS_CLK = 0x10,
    COINFT_V35_CTX_FRAME_MODE = 0x21,
    COINFT_V35_CTX_FRAME_SUBCONV = 0x2c,
    COINFT_V35_CTX_FRAME_CDAC = 0x2e,
    COINFT_V35_CTX_FRAME_COMP_DIV = 0x34,
    COINFT_V35_CTX_FRAME_LFSR_SCALE = 0x36
};

/* PSoC 4 MSCv3 register positions from the local Infineon device header. */
enum {
    COINFT_V37_SNS_SCAN_NUM_SUB_CONVS_POS = 0u,
    COINFT_V37_SNS_SCAN_COMP_DIV_POS = 16u,
    COINFT_V37_SNS_SCAN_NUM_CONV_POS = 28u,
    COINFT_V37_SNS_CTL_DECIM_RATE_POS = 8u,
    COINFT_V37_SNS_CTL_SENSE_DIV_POS = 16u,
    COINFT_V37_SNS_CTL_LFSR_MODE_POS = 28u,
    COINFT_V37_SNS_CTL_LFSR_BITS_POS = 30u,
    COINFT_V37_SNS_CDAC_SEL_RE_POS = 0u,
    COINFT_V37_SNS_CDAC_SEL_CO_POS = 8u,
    COINFT_V37_SNS_CDAC_CLOCK_REF_RATE = 0x00400000u
};

/* FUN_000012E8 takes two SNS_CTL mode fragments from outside the compact
 * 0x38-byte widget context.  The shared-context table is indexed relative to
 * +0x49; the common configuration contributes the multi-channel bits. */
enum {
    COINFT_V38_SHARED_SNS_MODE_BASE = 0x49u,
    COINFT_V38_COMMON_SNS_MODE_OFFSET = 0x4bu,
    COINFT_V38_CONTEXT_METHOD_SELECTOR = 0x31u,
    COINFT_V38_WIDGET_LFSR_FIELD = 0x36u,
    COINFT_V38_COMMON_DIVIDER_TYPE = 0x35u,
    COINFT_V38_COMMON_DIVIDER_INDEX = 0x36u,
    COINFT_V38_INTERNAL_PERIOD_DEFAULT = 0x36u
};

#define COINFT_V38_SNS_CTL_DECIM_MASK 0x0000ff00u

typedef struct {
    uint16_t raw_limit;
    uint16_t sns_clk;
    uint16_t row_sns_clk;
    uint8_t frame_lfsr_mode;
    uint16_t frame_sub_conversions;
    uint8_t frame_cdac_sel_re;
    uint16_t frame_comp_divider;
    uint8_t frame_lfsr_bits_encoded;
} coinft_v35_context_access_t;

static uint16_t coinft_v34_read_u16(const uint8_t *p);
static uint32_t coinft_v34_read_u32(const uint8_t *p);

typedef struct {
    uint16_t sense_tuning_word;
    uint8_t lfsr_bits;
    uint8_t tuning_high_byte;
} coinft_v38_tuning_view_t;

typedef struct {
    uint8_t divider_type;
    uint8_t divider_index;
} coinft_v38_common_divider_view_t;

/* PERI divider constants recovered from cyip_peri.h and FUN_00003f18/
 * FUN_00003f54/FUN_00004014/FUN_00004050.  The firmware accepts only the
 * divider counts below even though the silicon register arrays are larger. */
enum {
    COINFT_V39_PERI_DIV_CMD_OFFSET = 0x000u,
    COINFT_V39_PERI_DIV_16_OFFSET = 0x300u,
    COINFT_V39_PERI_DIV_16_5_OFFSET = 0x400u,
    COINFT_V39_PERI_DIV_24_5_OFFSET = 0x500u,
    COINFT_V39_PERI_DIV_16_COUNT = 15u,
    COINFT_V39_PERI_DIV_16_5_COUNT = 5u,
    COINFT_V39_PERI_DIV_24_5_COUNT = 1u,
    COINFT_V39_COMMON_DIVIDER_VALUE = 0x3fu,
    COINFT_V39_STATUS_OK = 0u,
    /* The four helper functions return the same observed BAD_PARAM literal. */
    COINFT_V39_STATUS_BAD_PARAM = 0x004a0001u
};

#define COINFT_V39_PERI_DIV_CMD_DISABLE UINT32_C(0x40000000)
#define COINFT_V39_PERI_DIV_CMD_ENABLE UINT32_C(0x8000ff00)
#define COINFT_V39_PERI_INT_MASK UINT32_C(0xff0000ff)
#define COINFT_V39_PERI_FRAC_MASK UINT32_C(0xffffff07)

typedef struct {
    uint32_t div_cmd;
    uint32_t div_16_ctl[COINFT_V39_PERI_DIV_16_COUNT];
    uint32_t div_16_5_ctl[COINFT_V39_PERI_DIV_16_5_COUNT];
    uint32_t div_24_5_ctl[COINFT_V39_PERI_DIV_24_5_COUNT];
    /* A short write log makes the disable -> set -> enable ordering testable. */
    uint32_t write_offsets[8];
    uint32_t write_values[8];
    size_t write_count;
} coinft_v39_peri_shadow_t;

typedef struct {
    uint8_t raw_divider_type;
    uint8_t effective_divider_type;
    uint8_t divider_index;
    uint8_t requested_divider;
} coinft_v39_common_divider_config_t;

/* The common record's +0x3f byte is the caller argument passed to
 * FUN_00001AF4.  +0x35/+0x36 select the divider family and slot. */
static bool coinft_v39_decode_common_divider_config(
    const uint8_t *common_config, size_t common_length,
    coinft_v39_common_divider_config_t *out)
{
    if (common_config == NULL || out == NULL ||
        common_length <= COINFT_V39_COMMON_DIVIDER_VALUE)
        return false;
    out->raw_divider_type = common_config[COINFT_V38_COMMON_DIVIDER_TYPE];
    out->effective_divider_type = out->raw_divider_type > 2u ? 3u :
                                  out->raw_divider_type;
    out->divider_index = common_config[COINFT_V38_COMMON_DIVIDER_INDEX];
    out->requested_divider = common_config[COINFT_V39_COMMON_DIVIDER_VALUE];
    return true;
}

static bool coinft_v39_peri_divider_valid(uint8_t divider_type,
                                          uint32_t divider_index)
{
    if (divider_type == 1u)
        return divider_index < COINFT_V39_PERI_DIV_16_COUNT;
    if (divider_type == 2u)
        return divider_index < COINFT_V39_PERI_DIV_16_5_COUNT;
    return divider_type == 3u && divider_index < COINFT_V39_PERI_DIV_24_5_COUNT;
}

static void coinft_v39_peri_record_write(coinft_v39_peri_shadow_t *shadow,
                                         uint32_t offset, uint32_t value)
{
    if (shadow->write_count < (sizeof(shadow->write_offsets) /
                               sizeof(shadow->write_offsets[0]))) {
        shadow->write_offsets[shadow->write_count] = offset;
        shadow->write_values[shadow->write_count] = value;
        shadow->write_count++;
    }
}

/* FUN_00004050 / Cy_SysClk_PeriphDisableDivider. */
static uint32_t coinft_v39_peri_disable_divider(
    coinft_v39_peri_shadow_t *shadow, uint8_t divider_type,
    uint32_t divider_index)
{
    uint32_t command;
    if (shadow == NULL || !coinft_v39_peri_divider_valid(divider_type,
                                                         divider_index))
        return COINFT_V39_STATUS_BAD_PARAM;
    command = COINFT_V39_PERI_DIV_CMD_DISABLE |
              ((uint32_t)divider_type << 6) | divider_index;
    shadow->div_cmd = command;
    coinft_v39_peri_record_write(shadow, COINFT_V39_PERI_DIV_CMD_OFFSET,
                                 command);
    return COINFT_V39_STATUS_OK;
}

/* FUN_00003f18.  This helper deliberately accepts only divider type 1. */
static uint32_t coinft_v39_peri_set_divider16(
    coinft_v39_peri_shadow_t *shadow, uint8_t divider_type,
    uint32_t divider_index, uint32_t divider_value)
{
    uint32_t value;
    if (shadow == NULL || divider_type != 1u ||
        divider_index >= COINFT_V39_PERI_DIV_16_COUNT ||
        divider_value >= 0x10000u)
        return COINFT_V39_STATUS_BAD_PARAM;
    value = (divider_value << 8) |
            (shadow->div_16_ctl[divider_index] & COINFT_V39_PERI_INT_MASK);
    shadow->div_16_ctl[divider_index] = value;
    coinft_v39_peri_record_write(shadow,
                                 COINFT_V39_PERI_DIV_16_OFFSET +
                                     divider_index * sizeof(uint32_t), value);
    return COINFT_V39_STATUS_OK;
}

/* FUN_00003f54.  The two writes and their masks are intentionally kept
 * separate: this is the observable behavior of the fractional helper. */
static uint32_t coinft_v39_peri_set_fractional_divider(
    coinft_v39_peri_shadow_t *shadow, uint8_t divider_type,
    uint32_t divider_index, uint32_t divider_integer,
    uint32_t divider_fraction)
{
    uint32_t value;
    if (shadow == NULL || divider_fraction >= 0x20u)
        return COINFT_V39_STATUS_BAD_PARAM;
    if (divider_type == 2u) {
        if (divider_index >= COINFT_V39_PERI_DIV_16_5_COUNT ||
            divider_integer >= 0x10000u)
            return COINFT_V39_STATUS_BAD_PARAM;
        value = (divider_integer << 8) |
                (shadow->div_16_5_ctl[divider_index] &
                 COINFT_V39_PERI_INT_MASK);
        shadow->div_16_5_ctl[divider_index] = value;
        coinft_v39_peri_record_write(shadow,
                                     COINFT_V39_PERI_DIV_16_5_OFFSET +
                                         divider_index * sizeof(uint32_t), value);
        value = (divider_fraction << 3) |
                (shadow->div_16_5_ctl[divider_index] &
                 COINFT_V39_PERI_FRAC_MASK);
        shadow->div_16_5_ctl[divider_index] = value;
        coinft_v39_peri_record_write(shadow,
                                     COINFT_V39_PERI_DIV_16_5_OFFSET +
                                         divider_index * sizeof(uint32_t), value);
        return COINFT_V39_STATUS_OK;
    }
    if (divider_type == 3u) {
        if (divider_index >= COINFT_V39_PERI_DIV_24_5_COUNT ||
            divider_integer >= 0x1000000u)
            return COINFT_V39_STATUS_BAD_PARAM;
        value = (divider_integer << 8) |
                (shadow->div_24_5_ctl[divider_index] & 0xffu);
        shadow->div_24_5_ctl[divider_index] = value;
        coinft_v39_peri_record_write(shadow,
                                     COINFT_V39_PERI_DIV_24_5_OFFSET +
                                         divider_index * sizeof(uint32_t), value);
        value = (divider_fraction << 3) |
                (shadow->div_24_5_ctl[divider_index] &
                 COINFT_V39_PERI_FRAC_MASK);
        shadow->div_24_5_ctl[divider_index] = value;
        coinft_v39_peri_record_write(shadow,
                                     COINFT_V39_PERI_DIV_24_5_OFFSET +
                                         divider_index * sizeof(uint32_t), value);
        return COINFT_V39_STATUS_OK;
    }
    return COINFT_V39_STATUS_BAD_PARAM;
}

/* FUN_00004014 / Cy_SysClk_PeriphEnableDivider.  The firmware also sets the
 * PA_SEL_TYPE/PA_SEL_DIV fields to all ones, i.e. uses clk_peri as reference. */
static uint32_t coinft_v39_peri_enable_divider(
    coinft_v39_peri_shadow_t *shadow, uint8_t divider_type,
    uint32_t divider_index)
{
    uint32_t command;
    if (shadow == NULL || !coinft_v39_peri_divider_valid(divider_type,
                                                         divider_index))
        return COINFT_V39_STATUS_BAD_PARAM;
    command = COINFT_V39_PERI_DIV_CMD_ENABLE |
              ((uint32_t)divider_type << 6) | divider_index;
    shadow->div_cmd = command;
    coinft_v39_peri_record_write(shadow, COINFT_V39_PERI_DIV_CMD_OFFSET,
                                 command);
    return COINFT_V39_STATUS_OK;
}

/* FUN_00001AF4.  `requested_divider` is the caller's one-based divider; the
 * register helpers receive the encoded value (requested - 1). */
static bool coinft_v39_apply_common_divider(
    coinft_v39_peri_shadow_t *shadow, const uint8_t *common_config,
    size_t common_length, uint32_t requested_divider)
{
    uint8_t divider_type;
    uint8_t divider_index;
    uint32_t encoded;
    bool ok;
    if (shadow == NULL || common_config == NULL ||
        common_length <= COINFT_V38_COMMON_DIVIDER_INDEX)
        return false;
    if (requested_divider == 0u)
        requested_divider = 1u;
    divider_type = common_config[COINFT_V38_COMMON_DIVIDER_TYPE];
    if (divider_type > 2u)
        divider_type = 3u;
    divider_index = common_config[COINFT_V38_COMMON_DIVIDER_INDEX];
    encoded = requested_divider - 1u;
    ok = coinft_v39_peri_disable_divider(shadow, divider_type,
                                         divider_index) ==
         COINFT_V39_STATUS_OK;
    if (divider_type < 2u) {
        ok = coinft_v39_peri_set_divider16(shadow, divider_type,
                                           divider_index, encoded) ==
                 COINFT_V39_STATUS_OK && ok;
    } else {
        ok = coinft_v39_peri_set_fractional_divider(
                 shadow, divider_type, divider_index, encoded, 0u) ==
                 COINFT_V39_STATUS_OK && ok;
    }
    ok = coinft_v39_peri_enable_divider(shadow, divider_type,
                                        divider_index) ==
             COINFT_V39_STATUS_OK && ok;
    return ok;
}

/* Full FUN_00001AF4 wrapper using all three common-config bytes. */
static bool coinft_v39_apply_common_divider_from_config(
    coinft_v39_peri_shadow_t *shadow, const uint8_t *common_config,
    size_t common_length)
{
    coinft_v39_common_divider_config_t config;
    if (!coinft_v39_decode_common_divider_config(common_config, common_length,
                                                 &config))
        return false;
    return coinft_v39_apply_common_divider(shadow, common_config,
                                           common_length,
                                           config.requested_divider);
}

/* Exact S593 PCLK destination numbering from psoc4100smax_config.h. */
enum {
    COINFT_V40_PCLK_SCB3 = 3u,
    COINFT_V40_PCLK_MSC0 = 5u,
    COINFT_V40_PCLK_TCPWM0 = 6u,
    COINFT_V40_PCLK_SAR = 18u,
    COINFT_V40_PCLK_MSC1 = 19u,
    COINFT_V40_PCLK_COUNT = 24u,
    COINFT_V40_DIVIDER16_BOOT0 = 0u,
    COINFT_V40_DIVIDER16_BOOT1 = 1u,
    COINFT_V40_DIVIDER16_BOOT2 = 2u,
    COINFT_V40_DIVIDER16_BOOT3 = 3u,
    COINFT_V40_DIVIDER16_BOOT2_VALUE = 5u,
    COINFT_V40_DIVIDER16_BOOT3_VALUE = 29u
};

typedef enum {
    COINFT_V40_PCLK_ROLE_UNKNOWN = 0,
    COINFT_V40_PCLK_ROLE_SCB3 = 1,
    COINFT_V40_PCLK_ROLE_MSC0 = 2,
    COINFT_V40_PCLK_ROLE_MSC1 = 3,
    COINFT_V40_PCLK_ROLE_SAR = 4,
    COINFT_V40_PCLK_ROLE_TCPWM0 = 5
} coinft_v40_pclk_role_t;

typedef struct {
    uint8_t pclk;
    uint8_t divider_type;
    uint8_t divider_index;
    coinft_v40_pclk_role_t role;
} coinft_v40_pclk_route_t;

typedef struct {
    uint8_t divider_type;
    uint8_t divider_index;
    uint16_t divider_value;
} coinft_v40_divider_boot_t;

/* FUN_00003fd8 writes PERI.PCLK_CTL[pclk] = (type << 6) | index. */
static bool coinft_v40_compose_pclk_route(
    uint8_t pclk, uint8_t divider_type, uint8_t divider_index,
    coinft_v40_pclk_role_t role, uint32_t *pclk_ctl,
    coinft_v40_pclk_route_t *route)
{
    if (pclk_ctl == NULL || pclk >= COINFT_V40_PCLK_COUNT ||
        !coinft_v39_peri_divider_valid(divider_type, divider_index))
        return false;
    *pclk_ctl = ((uint32_t)divider_type << 6) | divider_index;
    if (route != NULL) {
        route->pclk = pclk;
        route->divider_type = divider_type;
        route->divider_index = divider_index;
        route->role = role;
    }
    return true;
}

/* S593 firmware FUN_0000027c call order.  The SAR register writes between the
 * MSC1 and SAR routes are kept as an explicit phase marker. */
static const coinft_v40_pclk_route_t coinft_v40_boot_pclk_routes[] = {
    {COINFT_V40_PCLK_MSC0, 1u, 0u, COINFT_V40_PCLK_ROLE_MSC0},
    {COINFT_V40_PCLK_MSC1, 1u, 0u, COINFT_V40_PCLK_ROLE_MSC1},
    {COINFT_V40_PCLK_SAR, 1u, 3u, COINFT_V40_PCLK_ROLE_SAR},
    {COINFT_V40_PCLK_SCB3, 1u, 2u, COINFT_V40_PCLK_ROLE_SCB3},
    {COINFT_V40_PCLK_TCPWM0, 1u, 1u, COINFT_V40_PCLK_ROLE_TCPWM0}
};

static const coinft_v40_divider_boot_t coinft_v40_boot_dividers[] = {
    {1u, COINFT_V40_DIVIDER16_BOOT0, 0u},
    {1u, COINFT_V40_DIVIDER16_BOOT1, 0u},
    {1u, COINFT_V40_DIVIDER16_BOOT2, COINFT_V40_DIVIDER16_BOOT2_VALUE},
    {1u, COINFT_V40_DIVIDER16_BOOT3, COINFT_V40_DIVIDER16_BOOT3_VALUE}
};

static size_t coinft_v40_route_boot_pclks(uint32_t *pclk_ctl,
                                          size_t route_capacity)
{
    size_t written = 0u;
    size_t i;
    if (pclk_ctl == NULL)
        return 0u;
    for (i = 0u; i < sizeof(coinft_v40_boot_pclk_routes) /
                    sizeof(coinft_v40_boot_pclk_routes[0]); ++i) {
        uint32_t value;
        const coinft_v40_pclk_route_t *route = &coinft_v40_boot_pclk_routes[i];
        if (written >= route_capacity ||
            !coinft_v40_compose_pclk_route(route->pclk, route->divider_type,
                                            route->divider_index, route->role,
                                            &value, NULL))
            break;
        pclk_ctl[written++] = value;
    }
    return written;
}

typedef struct {
    uint8_t phase;
    uint8_t pclk_route_count;
    uint8_t divider_boot_count;
    uint8_t gpio_init_complete;
    uint8_t msc_clock_ready;
    uint8_t scb3_clock_ready;
    uint8_t sar_clock_ready;
} coinft_v40_startup_clock_state_t;

/* The 0x01d8 startup sequence has a stable high-level order in the image. */
static void coinft_v40_startup_clock_state(
    coinft_v40_startup_clock_state_t *state)
{
    if (state == NULL)
        return;
    state->phase = 0x1du; /* FUN_00000370: source/clock conditioning */
    state->divider_boot_count = (uint8_t)(sizeof(coinft_v40_boot_dividers) /
                                          sizeof(coinft_v40_boot_dividers[0]));
    state->phase = 0x02u; /* FUN_0000020c: DIV_16_CTL[0..3] */
    state->phase = 0x03u; /* FUN_0000034c: MSC common base words */
    state->phase = 0x27u; /* FUN_0000027c: PCLK route and SAR setup */
    state->pclk_route_count = (uint8_t)(sizeof(coinft_v40_boot_pclk_routes) /
                                        sizeof(coinft_v40_boot_pclk_routes[0]));
    state->msc_clock_ready = 1u;
    state->scb3_clock_ready = 1u;
    state->sar_clock_ready = 1u;
    state->phase = 0x2du; /* FUN_000002d0: GPIO/HSIOM application */
    state->gpio_init_complete = 1u;
}

enum {
    COINFT_V41_PIN_RECORD_SIZE = 8u,
    COINFT_V41_PIN_TABLE_BASE = 0x4608u,
    COINFT_V41_SHIELD_TABLE_BASE = 0x4698u,
    COINFT_V41_GPIO_HSIOM = 0u,
    COINFT_V41_ANALOG_DRIVE = 9u
};

typedef struct {
    uintptr_t port_base;
    uint8_t pin;
    uint8_t hsiom;
    uint8_t flags;
} coinft_v41_pin_record_t;

typedef struct {
    uintptr_t port_base;
    uint32_t dr;
    uint32_t pc;
    uint32_t pc2;
    uint32_t hsiom;
    uint32_t dr_clear_mask;
} coinft_v41_port_shadow_t;

/* The image stores GPIO register bases, while board documentation names the
 * same objects by physical port number.  Keep this translation explicit so a
 * host replay cannot accidentally match a pin by table position. */
static int coinft_v41_port_number(uintptr_t port_base)
{
    switch (port_base) {
    case (uintptr_t)0x40040000u:
        return 0;
    case (uintptr_t)0x40040300u:
        return 3;
    default:
        return -1;
    }
}

static bool coinft_v41_decode_pin_record(const uint8_t *flash, size_t length,
                                         size_t offset,
                                         coinft_v41_pin_record_t *out)
{
    if (flash == NULL || out == NULL || offset > length ||
        length - offset < COINFT_V41_PIN_RECORD_SIZE)
        return false;
    out->port_base = (uintptr_t)coinft_v34_read_u32(flash + offset);
    out->pin = flash[offset + 4u];
    out->hsiom = flash[offset + 5u];
    out->flags = flash[offset + 6u];
    return out->port_base != 0u && out->pin < 32u;
}

static bool coinft_v41_decode_pin_record_at(const uint8_t *flash,
                                            size_t length, size_t address,
                                            coinft_v41_pin_record_t *out)
{
    if (address < COINFT_V41_PIN_TABLE_BASE)
        return false;
    return coinft_v41_decode_pin_record(
        flash, length, address - COINFT_V41_PIN_TABLE_BASE, out);
}

/* FUN_00001750: replace one four-bit HSIOM nibble. */
static bool coinft_v41_set_hsiom(coinft_v41_port_shadow_t *port,
                                 unsigned pin, uint8_t hsiom)
{
    uint32_t shift;
    uint32_t mask;
    if (port == NULL || pin >= 8u || hsiom > 0xfu)
        return false;
    shift = pin * 4u;
    mask = UINT32_C(0xf) << shift;
    port->hsiom = (port->hsiom & ~mask) | ((uint32_t)hsiom << shift);
    return true;
}

/* FUN_0000177C: packed drive mode (bits 0..2) plus PC2.INP_DIS (bit 3). */
static bool coinft_v41_set_drive_mode(coinft_v41_port_shadow_t *port,
                                      unsigned pin, uint8_t packed_mode)
{
    uint32_t drive_shift;
    uint32_t drive_mask;
    uint32_t input_mask;
    if (port == NULL || pin >= 8u || packed_mode > 0xfu)
        return false;
    drive_shift = pin * 3u;
    drive_mask = UINT32_C(7) << drive_shift;
    input_mask = UINT32_C(1) << pin;
    port->pc = (port->pc & ~drive_mask) |
               ((uint32_t)(packed_mode & 7u) << drive_shift);
    port->pc2 = (port->pc2 & ~input_mask) |
                ((uint32_t)(packed_mode >> 3u) << pin);
    return true;
}

/* FUN_00001850 preserves the observed phase ordering around HSIOM and drive. */
static bool coinft_v41_set_pin_state(coinft_v41_port_shadow_t *port,
                                     unsigned pin, uint8_t packed_mode,
                                     uint8_t hsiom, uint8_t phase)
{
    if (port == NULL)
        return false;
    if (phase == 0u)
        return coinft_v41_set_hsiom(port, pin, COINFT_V41_GPIO_HSIOM) &&
               coinft_v41_set_drive_mode(port, pin, packed_mode);
    return coinft_v41_set_drive_mode(port, pin, packed_mode) &&
           coinft_v41_set_hsiom(port, pin, hsiom);
}

static bool coinft_v41_reset_pin(coinft_v41_port_shadow_t *port,
                                 const coinft_v41_pin_record_t *record)
{
    uint32_t bit;
    if (port == NULL || record == NULL ||
        !coinft_v41_set_pin_state(port, record->pin,
                                  COINFT_V41_ANALOG_DRIVE,
                                  COINFT_V41_GPIO_HSIOM, 0u))
        return false;
    bit = UINT32_C(1) << record->pin;
    port->dr &= ~bit;
    port->dr_clear_mask |= bit;
    return true;
}

/* FUN_00001896 prepares the two pins owned by one MSC/widget instance. */
static bool coinft_v41_reset_widget_pin_pair(
    coinft_v41_port_shadow_t *port_a, unsigned pin_a,
    coinft_v41_port_shadow_t *port_b, unsigned pin_b)
{
    coinft_v41_pin_record_t a = {0u, (uint8_t)pin_a, 0u, 0u};
    coinft_v41_pin_record_t b = {0u, (uint8_t)pin_b, 0u, 0u};
    if (port_a == NULL || port_b == NULL || pin_a >= 8u || pin_b >= 8u)
        return false;
    a.port_base = port_a->port_base;
    b.port_base = port_b->port_base;
    return coinft_v41_reset_pin(port_a, &a) &&
           coinft_v41_reset_pin(port_b, &b);
}

/* Apply a configured (non-reset) pin record while retaining its HSIOM route.
 * This is the inverse host operation needed to replay scan-time pin muxing;
 * the firmware's reset helpers intentionally use HSIOM=GPIO instead. */
static bool coinft_v41_apply_pin_record(coinft_v41_port_shadow_t *port,
                                        const coinft_v41_pin_record_t *record,
                                        uint8_t packed_mode, uint8_t phase)
{
    if (port == NULL || record == NULL ||
        port->port_base != record->port_base)
        return false;
    return coinft_v41_set_pin_state(port, record->pin, packed_mode,
                                    record->hsiom, phase);
}

/* FUN_000018C2/018F2 use two static tables and leave a per-port clear mask. */
static size_t coinft_v41_reset_pin_table(const uint8_t *flash, size_t length,
                                         size_t table_offset, size_t count,
                                         coinft_v41_port_shadow_t *ports,
                                         size_t port_count)
{
    size_t i;
    size_t reset = 0u;
    if (flash == NULL || ports == NULL)
        return 0u;
    for (i = 0u; i < count; ++i) {
        coinft_v41_pin_record_t record;
        size_t p;
        if (!coinft_v41_decode_pin_record_at(
                flash, length, table_offset + i * COINFT_V41_PIN_RECORD_SIZE,
                &record))
            break;
        for (p = 0u; p < port_count; ++p) {
            if (ports[p].port_base == record.port_base) {
                if (coinft_v41_reset_pin(&ports[p], &record))
                    ++reset;
                break;
            }
        }
    }
    return reset;
}

static size_t coinft_v41_reset_public_and_shield_tables(
    const uint8_t *flash, size_t length, coinft_v41_port_shadow_t *ports,
    size_t port_count, size_t public_count, size_t shield_count)
{
    size_t reset = coinft_v41_reset_pin_table(
        flash, length, COINFT_V41_PIN_TABLE_BASE, public_count,
        ports, port_count);
    reset += coinft_v41_reset_pin_table(
        flash, length, COINFT_V41_SHIELD_TABLE_BASE, shield_count,
        ports, port_count);
    return reset;
}

/* PSoC 4 SAR0 register offsets recovered from cyip_sar.h.  The firmware's
 * FUN_0000354c programs channels 0..15 plus one special channel-16 record. */
enum {
    COINFT_V42_SAR_BASE = 0x403a0000u,
    COINFT_V42_SAR_CTRL = 0x000u,
    COINFT_V42_SAR_SAMPLE_CTRL = 0x004u,
    COINFT_V42_SAR_SAMPLE_TIME01 = 0x010u,
    COINFT_V42_SAR_SAMPLE_TIME23 = 0x014u,
    COINFT_V42_SAR_RANGE_THRES = 0x018u,
    COINFT_V42_SAR_RANGE_COND = 0x01cu,
    COINFT_V42_SAR_CHAN_EN = 0x020u,
    COINFT_V42_SAR_CHAN_CONFIG = 0x080u,
    COINFT_V42_SAR_CHAN_RESULT = 0x180u,
    COINFT_V42_SAR_INTR = 0x210u,
    COINFT_V42_SAR_INTR_MASK = 0x218u,
    COINFT_V42_SAR_SATURATE_INTR = 0x220u,
    COINFT_V42_SAR_SATURATE_INTR_MASK = 0x228u,
    COINFT_V42_SAR_RANGE_INTR = 0x230u,
    COINFT_V42_SAR_RANGE_INTR_MASK = 0x238u,
    COINFT_V42_SAR_MUX_SWITCH0 = 0x300u,
    COINFT_V42_SAR_MUX_SWITCH_CLEAR0 = 0x304u,
    COINFT_V42_SAR_MUX_SWITCH_HW_CTRL = 0x340u,
    COINFT_V42_SAR_PUMP_CTRL = 0x380u,
    COINFT_V42_SAR_ANA_TRIM = 0xf00u,
    COINFT_V42_SAR_CHANNEL_COUNT = 17u,
    COINFT_V42_SAR_RECORD_SIZE = 7u,
    COINFT_V42_SAR_CONFIG_MIN = 0x7cu,
    COINFT_V42_SAR_COMMON_MASK = 0x03ff0000u,
    COINFT_V42_SAR_DEFAULT_CLOCK_BIAS = 0x01388000u,
    COINFT_V42_SAR_DEFAULT_OFFSET = (uint32_t)0xfffff800u,
    COINFT_V42_SAR_DEFAULT_MUX_CLEAR = 0x3fffffffu
};

typedef struct {
    uint8_t bytes[COINFT_V42_SAR_RECORD_SIZE];
    bool present;
} coinft_v42_sar_channel_record_t;

typedef struct {
    uint32_t ctrl;
    uint32_t sample_ctrl;
    uint32_t sample_time01;
    uint32_t sample_time23;
    uint32_t range_thres;
    uint32_t range_cond;
    uint32_t chan_en;
    uint32_t chan_config[COINFT_V42_SAR_CHANNEL_COUNT];
    uint32_t chan16_config;
    uint32_t intr;
    uint32_t intr_mask;
    uint32_t saturate_intr;
    uint32_t saturate_intr_mask;
    uint32_t range_intr;
    uint32_t range_intr_mask;
    uint32_t mux_switch0;
    uint32_t mux_switch_clear0;
    uint32_t mux_switch_hw_ctrl;
    uint32_t pump_ctrl;
    uint32_t ana_trim;
    int16_t channel_offset[COINFT_V42_SAR_CHANNEL_COUNT];
    uint32_t channel_timing[COINFT_V42_SAR_CHANNEL_COUNT];
    uint16_t chan_result[16];
} coinft_v42_sar_shadow_t;

static uint32_t coinft_v42_sar_channel_word(
    const coinft_v42_sar_channel_record_t *record)
{
    const uint8_t *p;
    if (record == NULL || !record->present)
        return 0u;
    p = record->bytes;
    return ((uint32_t)p[0] & 0x77u) |
           ((uint32_t)p[1] << 8u) |
           (((uint32_t)p[2] & 1u) << 9u) |
           ((uint32_t)p[3] << 10u) |
           (((uint32_t)p[4] & 3u) << 12u);
}

/* FUN_0000354c's common control-word packing.  The high DSI bits already
 * present in CTRL are preserved exactly as in the image. */
static bool coinft_v42_sar_pack_common(
    coinft_v42_sar_shadow_t *shadow, const uint8_t *config, size_t length)
{
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;
    if (shadow == NULL || config == NULL || length < COINFT_V42_SAR_CONFIG_MIN)
        return false;
    b1 = config[2u];
    b2 = config[4u];
    b3 = config[10u];
    shadow->ctrl = (shadow->ctrl & 0x30000000u) |
                   ((uint32_t)config[1u] << 7u) |
                   ((uint32_t)b2 << 20u) |
                   ((uint32_t)config[6u] << 27u) |
                   ((uint32_t)config[7u] << 30u) |
                   (((uint32_t)config[0u] & 7u) << 4u) |
                   (((uint32_t)b1 & 7u) << 9u) |
                   (((uint32_t)config[3u] & 1u) << 13u) |
                   (((uint32_t)config[5u] & 3u) << 24u);
    shadow->sample_ctrl = ((uint32_t)config[8u] & 1u) |
                          ((uint32_t)config[9u] << 1u) |
                          ((uint32_t)b3 << 2u) |
                          ((uint32_t)config[11u] << 3u) |
                          (((uint32_t)config[12u] & 7u) << 4u) |
                          ((uint32_t)config[13u] << 7u) |
                          (((uint32_t)config[14u] & 3u) << 17u) |
                          ((uint32_t)config[15u] << 31u);
    shadow->sample_time01 =
        ((uint32_t)coinft_v34_read_u16(config + 0x10u) & 0x3ffu) |
        (((uint32_t)coinft_v34_read_u16(config + 0x14u) << 16u) &
         COINFT_V42_SAR_COMMON_MASK);
    shadow->sample_time23 =
        ((uint32_t)coinft_v34_read_u16(config + 0x18u) & 0x3ffu) |
        (((uint32_t)coinft_v34_read_u16(config + 0x1cu) << 16u) &
         COINFT_V42_SAR_COMMON_MASK);
    shadow->range_thres = (uint32_t)coinft_v34_read_u16(config + 0x20u) |
                          ((uint32_t)coinft_v34_read_u16(config + 0x24u)
                           << 16u);
    shadow->range_cond = ((uint32_t)config[0x28u] << 30u);
    shadow->chan_en = (uint32_t)coinft_v34_read_u16(config + 0x2cu);
    shadow->pump_ctrl = (uint32_t)b2 << 31u;
    shadow->mux_switch_clear0 = COINFT_V42_SAR_DEFAULT_MUX_CLEAR;
    shadow->ctrl |= 0x20000000u;
    return true;
}

/* Decode one of the seven-byte channel records used by FUN_0000354c. */
static bool coinft_v42_sar_decode_channel_record(
    coinft_v42_sar_shadow_t *shadow,
    const coinft_v42_sar_channel_record_t *record, unsigned channel,
    uint32_t timing_token, uint8_t common_b1, uint8_t common_b3)
{
    const uint8_t *p;
    uint32_t flag_bit;
    int32_t offset = 0;
    if (shadow == NULL || record == NULL || !record->present ||
        channel >= COINFT_V42_SAR_CHANNEL_COUNT)
        return false;
    p = record->bytes;
    shadow->chan_config[channel] = coinft_v42_sar_channel_word(record);
    shadow->channel_timing[channel] = timing_token;
    if (p[1u] == 0u && common_b1 == 7u) {
        /* The signed expression is -(b3 == 0) & 0x800 plus -0x800. */
        offset = (common_b3 == 0u) ? 0 : (int32_t)COINFT_V42_SAR_DEFAULT_OFFSET;
    }
    (void)offset;
    if (channel < 16u) {
        flag_bit = 1u << channel;
        if (p[5u] != 0u) {
            shadow->range_intr |= flag_bit;
            shadow->range_intr_mask |= flag_bit;
        }
        if (p[6u] != 0u) {
            shadow->saturate_intr |= flag_bit;
            shadow->saturate_intr_mask |= flag_bit;
        }
        shadow->channel_offset[channel] = (int16_t)offset;
    }
    if (channel == 16u) {
        shadow->chan16_config = shadow->chan_config[channel];
        shadow->intr = ((uint32_t)p[5u] << 6u) |
                       ((uint32_t)p[6u] << 5u);
        shadow->intr_mask = shadow->intr;
    }
    return true;
}

/* Host replay of the complete SAR configuration path.  The timing token is
 * intentionally supplied by the caller because FUN_00004378 is a separate
 * clock conversion helper not recoverable from this image's symbols. */
static bool coinft_v42_configure_sar(
    coinft_v42_sar_shadow_t *shadow, const uint8_t *config, size_t length,
    const coinft_v42_sar_channel_record_t *records, size_t record_count,
    uint32_t timing_token, const uint32_t *mux_words)
{
    unsigned i;
    if (!coinft_v42_sar_pack_common(shadow, config, length) ||
        records == NULL || record_count > COINFT_V42_SAR_CHANNEL_COUNT)
        return false;
    for (i = 0u; i < record_count; ++i) {
        if (records[i].present &&
            !coinft_v42_sar_decode_channel_record(shadow, &records[i], i,
                                                   timing_token, config[2u],
                                                   config[10u]))
            return false;
    }
    if (mux_words != NULL) {
        shadow->mux_switch0 = mux_words[0];
        shadow->mux_switch_hw_ctrl = mux_words[1];
    }
    shadow->ana_trim = (shadow->ana_trim == 0u || shadow->ana_trim == 0x3fu)
                           ? 0x0bu : shadow->ana_trim;
    return true;
}

static bool coinft_v42_read_and_store_sar_result(
    coinft_v42_sar_shadow_t *shadow, unsigned channel, uint16_t *sram_result)
{
    if (shadow == NULL || sram_result == NULL || channel >= 16u)
        return false;
    /* FUN_00003816 returns a 16-bit result; FUN_00000528 then writes INTR=1. */
    *sram_result = shadow->chan_result[channel];
    shadow->intr = 1u;
    return true;
}

typedef enum {
    COINFT_V43_EVENT_RESET = 0,
    COINFT_V43_EVENT_CLOCKS_READY = 1,
    COINFT_V43_EVENT_PINS_RESET = 2,
    COINFT_V43_EVENT_SAR_CONFIGURED = 3,
    COINFT_V43_EVENT_CSD_SCAN_STARTED = 4,
    COINFT_V43_EVENT_CSD_SCAN_COMPLETE = 5,
    COINFT_V43_EVENT_SAR_RESULT_STORED = 6,
    COINFT_V43_EVENT_UART_FRAME_READY = 7
} coinft_v43_event_t;

typedef struct {
    coinft_v40_startup_clock_state_t clock;
    uint8_t public_pins_reset;
    uint8_t shield_pins_reset;
    uint8_t sar_configured;
    uint8_t csd_scan_active;
    uint8_t csd_scan_complete;
    uint8_t sar_result_valid;
    uint8_t uart_streaming;
    uint32_t sar_result_sram;
    uint32_t event_count;
    coinft_v43_event_t events[16];
} coinft_v43_runtime_state_t;

static void coinft_v43_record_event(coinft_v43_runtime_state_t *state,
                                    coinft_v43_event_t event)
{
    if (state == NULL)
        return;
    if (state->event_count < sizeof(state->events) / sizeof(state->events[0]))
        state->events[state->event_count++] = event;
}

/* Integrates the recovered initialization and steady-state boundaries.  It
 * deliberately leaves the CSD algorithm callback opaque while preserving the
 * ordering visible at the API/register level. */
static bool coinft_v43_begin_runtime(coinft_v43_runtime_state_t *state)
{
    if (state == NULL)
        return false;
    memset(state, 0, sizeof(*state));
    coinft_v40_startup_clock_state(&state->clock);
    coinft_v43_record_event(state, COINFT_V43_EVENT_RESET);
    coinft_v43_record_event(state, COINFT_V43_EVENT_CLOCKS_READY);
    state->public_pins_reset = 1u;
    state->shield_pins_reset = 1u;
    coinft_v43_record_event(state, COINFT_V43_EVENT_PINS_RESET);
    return true;
}

static bool coinft_v43_mark_sar_configured(coinft_v43_runtime_state_t *state)
{
    if (state == NULL || !state->public_pins_reset)
        return false;
    state->sar_configured = 1u;
    coinft_v43_record_event(state, COINFT_V43_EVENT_SAR_CONFIGURED);
    return true;
}

static bool coinft_v43_csd_scan_start(coinft_v43_runtime_state_t *state)
{
    if (state == NULL || !state->clock.gpio_init_complete ||
        !state->sar_configured || state->csd_scan_active)
        return false;
    state->csd_scan_active = 1u;
    state->csd_scan_complete = 0u;
    coinft_v43_record_event(state, COINFT_V43_EVENT_CSD_SCAN_STARTED);
    return true;
}

static bool coinft_v43_csd_scan_complete(coinft_v43_runtime_state_t *state)
{
    if (state == NULL || !state->csd_scan_active)
        return false;
    state->csd_scan_active = 0u;
    state->csd_scan_complete = 1u;
    coinft_v43_record_event(state, COINFT_V43_EVENT_CSD_SCAN_COMPLETE);
    return true;
}

static bool coinft_v43_store_sar_result(coinft_v43_runtime_state_t *state,
                                        uint16_t result)
{
    if (state == NULL)
        return false;
    state->sar_result_sram = result;
    state->sar_result_valid = 1u;
    coinft_v43_record_event(state, COINFT_V43_EVENT_SAR_RESULT_STORED);
    return true;
}

static bool coinft_v43_uart_frame_ready(coinft_v43_runtime_state_t *state)
{
    if (state == NULL || !state->csd_scan_complete || !state->uart_streaming)
        return false;
    coinft_v43_record_event(state, COINFT_V43_EVENT_UART_FRAME_READY);
    return true;
}

/* v44: dynamic sense-period/LFSR state recovered from FUN_00001bfc through
 * FUN_00001d18.  These fields are intentionally represented separately from
 * the modern CapSense typedef: the image uses a compact hybrid ABI. */
enum {
    COINFT_V44_DYNAMIC_SATURATED_SUM = 0xffffu,
    COINFT_V44_DYNAMIC_PERIOD_FALLBACK = 0x3ffu,
    COINFT_V44_DYNAMIC_ERROR_PERIOD = 0x800u,
    COINFT_V44_DYNAMIC_SCALE_SEED = 0xffffu,
    COINFT_V44_DYNAMIC_MAX_SENSE = 0x1000u
};

typedef struct {
    uint16_t sense_clk;
    uint16_t sub_conversions;
    uint8_t noise_percent;
    uint8_t mode;
    uint8_t tuning;
} coinft_v44_dynamic_widget_t;

typedef struct {
    uint8_t method;
    uint8_t compare_selector;
} coinft_v44_dynamic_config_t;

typedef struct {
    uint8_t widget_count;
    uint8_t period_bias;
    int8_t period_shift;
    uint16_t scale_word;
    uint16_t final_sub_conversions;
} coinft_v44_dynamic_environment_t;

typedef struct {
    uint16_t terminal_period;
    uint16_t terminal_sub_conversions;
    uint8_t updated_widgets;
    uint8_t mode0_guard_ok;
    uint32_t status;
} coinft_v44_dynamic_result_t;

/* FUN_00004378 is an unsigned quotient helper.  The original implementation
 * avoids division instructions, but its observable result is floor(a*b/d)
 * with 32-bit multiplication semantics. */
static uint32_t coinft_v44_mul_div_u32(uint32_t a, uint32_t b,
                                       uint32_t denominator)
{
    return denominator == 0u ? 0u : (uint32_t)(a * b) / denominator;
}

/* FUN_00001cb8 receives the percentage through a plain `char`.  Preserve its
 * signed extension before applying the firmware's 32-bit quotient helper. */
static uint32_t coinft_v44_mul_div_i32_as_u32(int32_t a, int32_t b,
                                              uint32_t denominator)
{
    uint32_t product = (uint32_t)((uint32_t)a * (uint32_t)b);
    return denominator == 0u ? 0u : product / denominator;
}

/* Thumb-1 register shifts use an 8-bit shift amount.  For a 32-bit value,
 * LSL/LSR by 32..255 yields zero; folding the amount with &31 would be a
 * different operation. */
static uint32_t coinft_v44_arm_lsl1(uint32_t shift_amount)
{
    uint32_t shift = shift_amount & 0xffu;
    return shift < 32u ? (UINT32_C(1) << shift) : 0u;
}

static uint32_t coinft_v44_arm_lsr_u32(uint32_t value,
                                       uint32_t shift_amount)
{
    uint32_t shift = shift_amount & 0xffu;
    return shift < 32u ? value >> shift : 0u;
}

/* FUN_00001bd8: select a power-of-two scale from the highest set bit in a
 * 16-bit value.  The firmware's DAT_00001bf8 seed is 0xffff. */
static uint32_t coinft_v44_msb_scale(uint16_t value, uint32_t seed)
{
    uint32_t bit = 0x8000u;
    uint32_t scale = seed;
    int bit_index = 15;
    while (bit_index != 0) {
        if (((uint32_t)value & bit) != 0u)
            return scale;
        --bit_index;
        scale >>= 1u;
        bit >>= 1u;
    }
    return 1u;
}

/* FUN_00001ba4.  Mode zero additionally calls an external validity helper;
 * the helper's business meaning is not present in the image, so its result is
 * supplied explicitly by the host replay. */
static bool coinft_v44_compare_dynamic(uint8_t selector,
                                       uint32_t threshold,
                                       uint32_t period_limit,
                                       uint32_t scale_limit,
                                       uint32_t raw_limit,
                                       bool mode0_guard_ok)
{
    bool ok = threshold <= period_limit;
    if (selector != 2u)
        ok = (scale_limit <= raw_limit) && ok;
    if (selector == 0u)
        ok = mode0_guard_ok && ok;
    return ok;
}

/* FUN_00001bfc returns either 0x08 or 0x0a. */
static uint8_t coinft_v44_compute_mode8(
    const coinft_v44_dynamic_widget_t *widget,
    const coinft_v44_dynamic_environment_t *environment)
{
    uint32_t sum;
    uint32_t scale;
    if (widget == NULL || environment == NULL)
        return 8u;
    sum = (uint32_t)environment->period_bias +
          (uint32_t)widget->sub_conversions;
    if (sum > COINFT_V44_DYNAMIC_SATURATED_SUM)
        sum = COINFT_V44_DYNAMIC_SATURATED_SUM;
    scale = coinft_v44_msb_scale(environment->scale_word,
                                 COINFT_V44_DYNAMIC_SCALE_SEED);
    return (((scale + 1u) >> 2u) <= sum) ? 0x0au : 0x08u;
}

/* FUN_00001cb8/FUN_00001cec. */
static uint8_t coinft_v44_compute_tuning(
    const coinft_v44_dynamic_widget_t *widget,
    const coinft_v44_dynamic_config_t *config,
    int8_t period_shift)
{
    uint32_t scaled;
    uint32_t delta;
    uint32_t bounded;
    uint32_t base;
    uint8_t code = 3u;
    if (widget == NULL || config == NULL)
        return 0x83u;
    base = config->method == 1u ? 8u : 4u;
    scaled = coinft_v44_mul_div_i32_as_u32(
        (int32_t)widget->sense_clk,
        (int32_t)(int8_t)widget->noise_percent, 100u);
    scaled = coinft_v44_arm_lsr_u32(scaled, (uint8_t)period_shift);
    delta = coinft_v44_arm_lsr_u32((uint32_t)widget->sense_clk - base,
                                   (uint8_t)period_shift);
    bounded = scaled < delta ? scaled : delta;
    if (bounded < 0x10u) {
        code = 2u;
        if (bounded < 8u)
            code = (uint8_t)(3u < bounded);
    }
    return (uint8_t)(code | 0x80u);
}

/* FUN_00001c24.  The returned byte is the new +0x21 state and always carries
 * the 0x04 flag; all arithmetic is kept unsigned to match Thumb codegen. */
static uint8_t coinft_v44_compute_dynamic_mode(
    const coinft_v44_dynamic_widget_t *widget,
    const coinft_v44_dynamic_config_t *config,
    const coinft_v44_dynamic_environment_t *environment,
    bool mode0_guard_ok)
{
    uint32_t sum;
    uint32_t threshold;
    uint32_t period_limit;
    uint32_t scale_limit;
    uint32_t scaled;
    uint32_t shift;
    uint8_t threshold_shift;
    uint32_t base;
    if (widget == NULL || config == NULL || environment == NULL)
        return 4u;
    sum = (uint32_t)environment->period_bias +
          (uint32_t)widget->sub_conversions;
    if (sum > COINFT_V44_DYNAMIC_SATURATED_SUM)
        sum = COINFT_V44_DYNAMIC_SATURATED_SUM;
    shift = (uint32_t)(uint8_t)environment->period_shift;
    base = config->method == 1u ? 8u : 4u;
    scaled = coinft_v44_mul_div_u32(widget->sense_clk,
                                    (uint32_t)(uint8_t)widget->noise_percent,
                                    100u);
    scaled = coinft_v44_arm_lsr_u32(scaled, shift);
    period_limit = coinft_v44_arm_lsr_u32(
        (uint32_t)widget->sense_clk - base, shift);
    if (scaled < period_limit)
        period_limit = scaled;
    threshold_shift = (uint8_t)((int32_t)(widget->tuning & 3u) +
                                (int32_t)environment->period_shift + 1);
    threshold = coinft_v44_arm_lsl1(threshold_shift);
    scale_limit = coinft_v44_msb_scale(environment->scale_word,
                                        COINFT_V44_DYNAMIC_SCALE_SEED);
    return (uint8_t)(coinft_v44_compare_dynamic(
                         config->compare_selector, threshold, period_limit,
                         scale_limit, sum, mode0_guard_ok) ? 1u : 0u) | 4u;
}

static uint32_t coinft_v44_period_shift_count(
    int8_t period_shift, uint8_t tuning)
{
    return ((uint32_t)(uint8_t)period_shift + 1u +
            (uint32_t)(tuning & 3u)) & 0xffu;
}

/* One iteration of FUN_00001d18's widget loop. */
static uint32_t coinft_v44_update_dynamic_widget(
    coinft_v44_dynamic_widget_t *widget,
    const coinft_v44_dynamic_config_t *config,
    const coinft_v44_dynamic_environment_t *environment,
    bool mode0_guard_ok)
{
    uint8_t old_mode;
    uint32_t minimum_period;
    if (widget == NULL || config == NULL || environment == NULL)
        return COINFT_V44_DYNAMIC_ERROR_PERIOD;
    old_mode = widget->mode;
    if (((old_mode & 4u) == 0u) && ((old_mode & 3u) != 1u)) {
        if ((old_mode & 8u) != 0u)
            widget->mode = coinft_v44_compute_mode8(widget, environment);
    } else {
        if (widget->tuning > 0x7fu)
            widget->tuning = coinft_v44_compute_tuning(
                widget, config, environment->period_shift);
        if ((old_mode & 4u) != 0u)
            widget->mode = coinft_v44_compute_dynamic_mode(
                widget, config, environment, mode0_guard_ok);
    }
    minimum_period = config->method == 1u ? 8u : 4u;
    if ((widget->mode & 3u) == 1u)
        minimum_period += coinft_v44_arm_lsl1(coinft_v44_period_shift_count(
            environment->period_shift, widget->tuning));
    if ((uint32_t)widget->sense_clk < minimum_period ||
        (uint32_t)widget->sense_clk > COINFT_V44_DYNAMIC_MAX_SENSE)
        return COINFT_V44_DYNAMIC_ERROR_PERIOD;
    return 0u;
}

/* Complete FUN_00001d18 replay.  `mode0_guard_ok` stands in for the opaque
 * callback reached by FUN_00004484 when compare_selector==0. */
static bool coinft_v44_update_dynamic_widgets(
    coinft_v44_dynamic_widget_t *widgets,
    const coinft_v44_dynamic_config_t *configs,
    size_t widget_count,
    coinft_v44_dynamic_environment_t *environment,
    bool mode0_guard_ok,
    coinft_v44_dynamic_result_t *result)
{
    size_t i;
    uint32_t mode1_count = 0u;
    if (widgets == NULL || configs == NULL || environment == NULL ||
        result == NULL || widget_count > 0xffu)
        return false;
    memset(result, 0, sizeof(*result));
    result->mode0_guard_ok = mode0_guard_ok ? 1u : 0u;
    for (i = 0u; i < widget_count; ++i) {
        uint32_t status = coinft_v44_update_dynamic_widget(
            &widgets[i], &configs[i], environment, mode0_guard_ok);
        if (status != 0u) {
            result->status = status;
            return false;
        }
        if ((widgets[i].mode & 2u) != 0u)
            ++mode1_count;
        ++result->updated_widgets;
    }
    if (mode1_count == 0u) {
        result->terminal_sub_conversions = environment->final_sub_conversions;
        result->terminal_period = 3u;
    } else {
        result->terminal_sub_conversions = COINFT_V44_DYNAMIC_PERIOD_FALLBACK;
        result->terminal_period = COINFT_V44_DYNAMIC_PERIOD_FALLBACK;
    }
    result->status = 0u;
    return true;
}

static bool coinft_v38_decode_tuning(const uint8_t *context_bytes,
                                     size_t context_length,
                                     coinft_v38_tuning_view_t *out)
{
    if (context_bytes == NULL || out == NULL ||
        context_length <= COINFT_V38_WIDGET_LFSR_FIELD + 1u)
        return false;
    out->sense_tuning_word = coinft_v34_read_u16(
        context_bytes + COINFT_V38_WIDGET_LFSR_FIELD);
    out->lfsr_bits = (uint8_t)(context_bytes[COINFT_V38_WIDGET_LFSR_FIELD] & 3u);
    out->tuning_high_byte = context_bytes[COINFT_V38_WIDGET_LFSR_FIELD + 1u];
    return true;
}

/* FUN_00001AF4 uses a different base: *component points at common config,
 * where +0x35/+0x36 select the PERI divider type and index. */
static bool coinft_v38_decode_common_divider(
    const uint8_t *common_config, size_t common_length,
    coinft_v38_common_divider_view_t *out)
{
    if (common_config == NULL || out == NULL ||
        common_length <= COINFT_V38_COMMON_DIVIDER_INDEX)
        return false;
    out->divider_type = common_config[COINFT_V38_COMMON_DIVIDER_TYPE];
    out->divider_index = common_config[COINFT_V38_COMMON_DIVIDER_INDEX];
    return out->divider_type >= 1u && out->divider_type <= 3u;
}

/* The widget-context +0x36 word is consumed by FUN_00001C24/FUN_00001D18 for
 * dynamic clock/tuning decisions.  Only its low two bits are copied into the
 * MSCv3 SNS_CTL.LFSR_BITS field by FUN_000012E8.  FUN_00001AF4's +0x35/+0x36
 * access belongs to common config and is decoded separately above. */
static uint32_t coinft_v38_compose_sns_ctl_modes(
    uint32_t base_sns_ctl, uint8_t widget_method,
    uint8_t context_method_selector, const uint8_t *shared_context,
    size_t shared_length, const uint8_t *common_config, size_t common_length)
{
    uint8_t shared_index;
    uint8_t shared_mode = 0u;
    uint8_t common_mode = 0u;
    if (shared_context != NULL && shared_length > COINFT_V38_SHARED_SNS_MODE_BASE) {
        /* Machine code uses method+1 only when +0x31 equals one; otherwise
         * it uses method-1.  Guard the unsigned underflow for host callers. */
        if (context_method_selector == 1u) {
            shared_index = (uint8_t)(widget_method + 1u);
        } else {
            shared_index = widget_method == 0u ? 0u : (uint8_t)(widget_method - 1u);
        }
        if ((size_t)COINFT_V38_SHARED_SNS_MODE_BASE + shared_index < shared_length)
            shared_mode = shared_context[COINFT_V38_SHARED_SNS_MODE_BASE + shared_index];
    }
    if (common_config != NULL && common_length > COINFT_V38_COMMON_SNS_MODE_OFFSET)
        common_mode = common_config[COINFT_V38_COMMON_SNS_MODE_OFFSET];
    return base_sns_ctl | ((uint32_t)shared_mode << 6)
         | ((uint32_t)common_mode << 4);
}

/* FUN_00001238 starts the normal sensor image with SNS_CTL == 0 and only
 * writes the sense divider, LFSR fields, mode bits and START/VALID.  Its
 * masks preserve no bits 8..15, so the normal generated image has
 * DECIM_RATE == 0.  The only recovered alternative sources are the complete
 * shared-context words used by FUN_0000207C's special pair branch. */
static uint8_t coinft_v38_normal_decim_rate(uint32_t sns_ctl)
{
    return (uint8_t)((sns_ctl & COINFT_V38_SNS_CTL_DECIM_MASK) >> 8);
}

static bool coinft_v38_decode_special_sns_ctl_sources(
    const uint8_t *shared_context, size_t shared_length,
    uint32_t *instance0_sns_ctl, uint32_t *instance1_sns_ctl)
{
    if (shared_context == NULL || instance0_sns_ctl == NULL ||
        instance1_sns_ctl == NULL || shared_length < 0x28u)
        return false;
    *instance0_sns_ctl = coinft_v34_read_u32(shared_context + 0x20u) | 5u;
    *instance1_sns_ctl = coinft_v34_read_u32(shared_context + 0x24u) | 5u;
    return true;
}

/* These offsets are consumed by CoinFT's recovered code, not a modern
 * CapSense typedef.  The distinction preserves the hybrid ABI found in v34. */
static bool coinft_v35_decode_context_access(const uint8_t *bytes,
                                             size_t length,
                                             coinft_v35_context_access_t *out)
{
    if (bytes == NULL || out == NULL || length < 0x38u)
        return false;
    out->raw_limit = coinft_v34_read_u16(bytes + COINFT_V35_CTX_RAW_LIMIT);
    out->sns_clk = coinft_v34_read_u16(bytes + COINFT_V35_CTX_SNS_CLK);
    out->row_sns_clk = coinft_v34_read_u16(bytes + COINFT_V35_CTX_ROW_SNS_CLK);
    out->frame_lfsr_mode = bytes[COINFT_V35_CTX_FRAME_MODE];
    out->frame_sub_conversions =
        coinft_v34_read_u16(bytes + COINFT_V35_CTX_FRAME_SUBCONV);
    out->frame_cdac_sel_re = bytes[COINFT_V35_CTX_FRAME_CDAC];
    out->frame_comp_divider =
        coinft_v34_read_u16(bytes + COINFT_V35_CTX_FRAME_COMP_DIV);
    out->frame_lfsr_bits_encoded = bytes[COINFT_V35_CTX_FRAME_LFSR_SCALE];
    return true;
}

static uint32_t coinft_v35_encoded_minus_one(uint32_t value)
{
    /* Ghidra's unsigned expression is value + ((value == 0) - 1):
     * zero remains zero, otherwise the value is decremented. */
    return value == 0u ? 0u : value - 1u;
}

/* Reconcile FUN_00001238/FUN_000012E8 with the byte-accurate context view. */
static bool coinft_v35_build_sensor_frame_from_context(
    const uint8_t *context_bytes, size_t context_length,
    const coinft_widget_record_t *widget, unsigned sensor,
    uint8_t sensor_record_byte9, uint8_t common_flag,
    uint8_t num_conversions, coinft_frame_t *out)
{
    coinft_v35_context_access_t access;
    uint16_t sense;
    uint32_t scan_ctl;
    if (widget == NULL || out == NULL ||
        !coinft_v35_decode_context_access(context_bytes, context_length, &access))
        return false;
    sense = access.sns_clk;
    if (widget->method == 1u && sensor >= widget->alternate_sensor_start)
        sense = access.row_sns_clk;
    sense = coinft_adjust_sense(widget->method, access.frame_lfsr_mode, sense);
    scan_ctl = (coinft_v35_encoded_minus_one((uint32_t)num_conversions) << 28)
             | (coinft_v35_encoded_minus_one(access.frame_comp_divider)
                << COINFT_V37_SNS_SCAN_COMP_DIV_POS)
             | (coinft_v35_encoded_minus_one(access.frame_sub_conversions)
                << COINFT_V37_SNS_SCAN_NUM_SUB_CONVS_POS);
    out->csw[0] = 0u;
    out->csw[1] = 0u;
    out->csw[2] = 0u;
    out->scan_ctl = scan_ctl & 0x7fffffffu;
    out->cdac_ctl = COINFT_V37_SNS_CDAC_CLOCK_REF_RATE
                  | ((uint32_t)sensor_record_byte9
                     << COINFT_V37_SNS_CDAC_SEL_CO_POS)
                  | ((uint32_t)access.frame_cdac_sel_re
                     << COINFT_V37_SNS_CDAC_SEL_RE_POS);
    out->sns_ctl = ((uint32_t)coinft_v35_encoded_minus_one(sense)
                    << COINFT_V37_SNS_CTL_SENSE_DIV_POS)
                 | ((uint32_t)(access.frame_lfsr_bits_encoded & 3u)
                    << COINFT_V37_SNS_CTL_LFSR_BITS_POS)
                 | ((uint32_t)(access.frame_lfsr_mode & 3u)
                    << COINFT_V37_SNS_CTL_LFSR_MODE_POS)
                 | ((uint32_t)common_flag << 4)
                 | 3u;
    return true;
}

/* v38 entry point: keep the proven v37 frame construction, then add the two
 * mode fragments that FUN_000012E8 reads from the shared/common objects. */
static bool coinft_v38_build_sensor_frame_from_context(
    const uint8_t *context_bytes, size_t context_length,
    const coinft_widget_record_t *widget, unsigned sensor,
    uint8_t sensor_record_byte9, uint8_t num_conversions,
    const uint8_t *shared_context, size_t shared_length,
    const uint8_t *common_config, size_t common_length,
    coinft_frame_t *out)
{
    if (!coinft_v35_build_sensor_frame_from_context(
            context_bytes, context_length, widget, sensor,
            sensor_record_byte9, 0u, num_conversions, out))
        return false;
    out->sns_ctl = coinft_v38_compose_sns_ctl_modes(
        out->sns_ctl, widget->method, context_bytes[COINFT_V38_CONTEXT_METHOD_SELECTOR],
        shared_context, shared_length, common_config, common_length);
    return true;
}

static uint16_t coinft_v34_read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t coinft_v34_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Decode the modern fifth-generation candidate overlay from a captured context.
 * CoinFT-specific consumers must still use the disambiguation table in v34. */
static bool coinft_v34_decode_widget_context(const uint8_t *bytes,
                                             size_t length,
                                             coinft_v34_context_candidate_t *out)
{
    if (bytes == NULL || out == NULL || length < 0x38u)
        return false;
    out->finger_cap = coinft_v34_read_u16(bytes + 0x00u);
    out->sig_pfc = coinft_v34_read_u16(bytes + 0x02u);
    out->max_raw_count = coinft_v34_read_u16(bytes + 0x04u);
    out->max_raw_count_row = coinft_v34_read_u16(bytes + 0x06u);
    out->finger_th = coinft_v34_read_u16(bytes + 0x08u);
    out->prox_th = coinft_v34_read_u16(bytes + 0x0au);
    out->low_bsln_rst = coinft_v34_read_u16(bytes + 0x0cu);
    out->sns_clk = coinft_v34_read_u16(bytes + 0x0eu);
    out->row_sns_clk = coinft_v34_read_u16(bytes + 0x10u);
    out->gesture_detected = coinft_v34_read_u32(bytes + 0x14u);
    out->gesture_direction = coinft_v34_read_u32(bytes + 0x18u);
    out->x_delta = (int16_t)coinft_v34_read_u16(bytes + 0x1cu);
    out->y_delta = (int16_t)coinft_v34_read_u16(bytes + 0x1eu);
    out->noise_th = coinft_v34_read_u16(bytes + 0x20u);
    out->n_noise_th = coinft_v34_read_u16(bytes + 0x22u);
    out->hysteresis = coinft_v34_read_u16(bytes + 0x24u);
    out->on_debounce = bytes[0x26u];
    out->sns_clk_source = bytes[0x27u];
    out->num_sub_conversions = coinft_v34_read_u16(bytes + 0x28u);
    out->cdac_ref = bytes[0x2au];
    out->row_cdac_ref = bytes[0x2bu];
    out->cic_rate = bytes[0x2cu];
    out->cdac_dither_en = bytes[0x2du];
    out->cdac_dither_value = bytes[0x2eu];
    out->coarse_init_bypass_en = bytes[0x2fu];
    out->cdac_comp_divider = coinft_v34_read_u16(bytes + 0x30u);
    out->lfsr_bits = bytes[0x32u];
    out->reserved_padding = bytes[0x33u];
    out->reserved0 = coinft_v34_read_u32(bytes + 0x34u);
    return true;
}

/* FUN_00001924: mark one logical channel active in the component activity
 * word.  The recovered callers pass channels 0 and 1; keeping the bit guard
 * makes this host model defined for malformed inputs as well. */
static void coinft_v36_mark_activity(uint32_t *activity_word,
                                     unsigned channel)
{
    uint32_t channel_bit = 0u;
    if (activity_word == NULL)
        return;
    if (channel < 32u)
        channel_bit = 1u << channel;
    *activity_word |= channel_bit | 0x80u;
}

/* FUN_000027B0: this byte is a proven enable flag in the 0x38-byte widget
 * context.  It is kept byte-oriented because the surrounding ABI is hybrid. */
static bool coinft_v36_widget_context_enabled(const uint8_t *context_bytes,
                                              size_t context_length)
{
    return context_bytes != NULL && context_length > 0x23u &&
           (context_bytes[0x23u] & 6u) == 6u;
}

typedef void (*coinft_v36_component_callback_fn)(uint8_t *context);

typedef struct {
    const uint8_t *shared_context;
    uint32_t *activity_word;
    uint32_t *msc_runtime[COINFT_INSTANCE_COUNT];
    volatile uint32_t *msc_sns_ctl[COINFT_INSTANCE_COUNT];
    uint8_t *callback_base;
    coinft_v36_component_callback_fn callback;
    bool callback_is_reset;
} coinft_v36_rearm_view_t;

/* This is the exact side-effect order of FUN_0000207C's special branch.
 * Names such as "calibration" or "restart" are intentionally avoided: the
 * machine code proves a pair re-arm/write sequence, but not its business role. */
static bool coinft_v36_special_pair_rearm(
    const coinft_v36_rearm_view_t *view,
    unsigned candidate_slot,
    unsigned backtrack_count)
{
    uint32_t sns_ctl0;
    uint32_t sns_ctl1;
    if (view == NULL || view->shared_context == NULL ||
        view->activity_word == NULL || view->msc_runtime[0] == NULL ||
        view->msc_runtime[1] == NULL || view->msc_sns_ctl[0] == NULL ||
        view->msc_sns_ctl[1] == NULL)
        return false;
    if (view->shared_context[0x43u] != 2u ||
        (unsigned)coinft_v34_read_u16(view->shared_context + 0x2cu) !=
            candidate_slot || backtrack_count != 1u ||
        (*view->activity_word & 0x2000u) != 0u ||
        view->shared_context[0x40u] != 1u)
        return false;

    view->msc_runtime[0][0x190u / sizeof(uint32_t)] = 1u;
    view->msc_runtime[1][0x190u / sizeof(uint32_t)] = 1u;

    /* FUN_00001924(0, component), callback(component_context). */
    coinft_v36_mark_activity(view->activity_word, 0u);
    if (view->callback != NULL && !view->callback_is_reset)
        view->callback(view->callback_base);
    sns_ctl0 = coinft_v34_read_u32(view->shared_context + 0x20u) | 5u;
    *view->msc_sns_ctl[0] = sns_ctl0;

    /* FUN_00001924(1, component), callback(component_context + 0x18). */
    coinft_v36_mark_activity(view->activity_word, 1u);
    if (view->callback != NULL && !view->callback_is_reset)
        view->callback(view->callback_base == NULL
                           ? NULL : view->callback_base + 0x18u);
    sns_ctl1 = coinft_v34_read_u32(view->shared_context + 0x24u) | 5u;
    *view->msc_sns_ctl[1] = sns_ctl1;
    return true;
}

/* v45: synchronized read-only evidence model.  These helpers describe the
 * observations made while a live board was streaming; they do not access
 * KitProg3, UART, or MMIO themselves. */
enum {
    COINFT_V45_UART_FRAME_SIZE = 26u,
    COINFT_V45_UART_PAYLOAD_COUNT = 12u,
    COINFT_V45_UART_SOF = 0x02u,
    COINFT_V45_UART_EOF = 0x03u,
    COINFT_V45_FULL_SRAM_SIZE = 0x8000u,
    COINFT_V45_STREAM_SRAM_SIZE = 0x1000u
};

typedef struct {
    uint16_t values[COINFT_V45_UART_PAYLOAD_COUNT];
} coinft_v45_uart_frame_t;

static bool coinft_v45_decode_uart_frame(const uint8_t *bytes, size_t length,
                                         coinft_v45_uart_frame_t *out)
{
    unsigned index;
    if (bytes == NULL || out == NULL || length != COINFT_V45_UART_FRAME_SIZE ||
        bytes[0] != COINFT_V45_UART_SOF ||
        bytes[COINFT_V45_UART_FRAME_SIZE - 1u] != COINFT_V45_UART_EOF)
        return false;
    for (index = 0u; index < COINFT_V45_UART_PAYLOAD_COUNT; ++index)
        out->values[index] = coinft_v34_read_u16(bytes + 1u + index * 2u);
    return true;
}

static uint32_t coinft_v45_set_last_bit(uint32_t sns_ctl, bool last)
{
    return last ? (sns_ctl | 0x04u) : (sns_ctl & ~UINT32_C(0x04));
}

static bool coinft_v45_sram_dump_usable(size_t dump_length, bool read_error)
{
    return !read_error && dump_length == COINFT_V45_FULL_SRAM_SIZE;
}

typedef struct {
    uint32_t status_word;
    uint32_t runtime_flag;
    uint32_t sensor_frame[6];
    bool sram_dump_complete;
} coinft_v45_msc_observation_t;

/* A status word of 0x1000 or 0x01001000 was observed at +0x170.  The
 * individual hardware bit names remain unresolved, so this predicate only
 * exposes the stable observed flag and deliberately avoids over-naming it. */
static bool coinft_v45_observed_scan_flag(
    const coinft_v45_msc_observation_t *observation)
{
    return observation != NULL &&
           (observation->status_word & UINT32_C(0x1000)) != 0u;
}

static bool coinft_v45_frame_state_equal(
    const coinft_v45_msc_observation_t *left,
    const coinft_v45_msc_observation_t *right)
{
    unsigned index;
    if (left == NULL || right == NULL)
        return false;
    for (index = 0u; index < 6u; ++index) {
        if (left->sensor_frame[index] != right->sensor_frame[index])
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------------
 * v46: evidence from a single CoinFT board observed through one KitProg3.
 *
 * Earlier rounds could only pair SWD reads of one board with UART frames of a
 * *different* board, so no SRAM address could be tied to a transmitted value.
 * v46 uses the KitProg3's own USB-CDC bridge, which is wired to the same board
 * the SWD probe is attached to, and therefore closes that gap.
 * ---------------------------------------------------------------------- */

enum {
    /* 13 x 10-byte sensor records.  The array begins exactly where the 13
     * widget contexts end (0x184 + 13 * 0x38 == 0x45c), and it holds the same
     * number of entries as the context array. */
    COINFT_V46_SENSOR_RECORD_BASE = 0x2000045C,
    COINFT_V46_SENSOR_RECORD_STRIDE = 10,
    COINFT_V46_SENSOR_RECORD_COUNT = 13,
    /* Only records 0..11 are transmitted; record 12 belongs to the index-12
     * widget, which never appears in the UART payload. */
    COINFT_V46_UART_CHANNEL_COUNT = 12,

    /* Per-channel history array.  Base is 0x2000091a, not 0x20000918: the
     * halfword at 0x20000918 is the tail of the preceding object. */
    COINFT_V46_HISTORY_BASE = 0x2000091A,
    COINFT_V46_HISTORY_STRIDE = 10,
    COINFT_V46_HISTORY_COUNT = 13,
    COINFT_V46_HISTORY_SLOTS = 5
};

/* MSCv3 register offsets, named from the official cyip_msc.h rather than from
 * the previous rounds' provisional labels. */
enum {
    COINFT_V46_MSC_STATUS1 = 0x100,
    COINFT_V46_MSC_STATUS3 = 0x108,
    COINFT_V46_MSC_INTR = 0x170,
    COINFT_V46_MSC_INTR_MASK = 0x178,
    COINFT_V46_MSC_FRAME_CMD = 0x190,
    COINFT_V46_MSC_SNS_SW_SEL_CSW_MASK2 = 0x20C,
    COINFT_V46_MSC_SNS_SW_SEL_CSW_MASK1 = 0x210,
    COINFT_V46_MSC_SNS_SW_SEL_CSW_MASK0 = 0x214,
    COINFT_V46_MSC_SNS_SCAN_CTL = 0x218,
    COINFT_V46_MSC_SNS_CDAC_CTL = 0x21C,
    COINFT_V46_MSC_SNS_CTL = 0x220
};

/* MSC.INTR causes.  v45 could only call bit 12 "an observed scan flag"; the
 * header resolves it, and the live target confirms the pattern. */
enum {
    COINFT_V46_INTR_SUB_SAMPLE = 0x00000001u,
    COINFT_V46_INTR_SAMPLE = 0x00000010u,
    COINFT_V46_INTR_SCAN = 0x00000100u,
    COINFT_V46_INTR_INIT = 0x00001000u,
    COINFT_V46_INTR_FRAME = 0x00010000u,
    COINFT_V46_INTR_CIC2_ERROR = 0x00100000u,
    COINFT_V46_INTR_CONFIG_REQ = 0x01000000u,
    COINFT_V46_INTR_FIFO_UNDERFLOW = 0x40000000u,
    COINFT_V46_INTR_FIFO_OVERFLOW = 0x80000000u
};

/* MSC.FRAME_CMD; the write that v36 could only describe as
 * "msc_runtime[i][0x190] = 1" is a START_FRAME command. */
enum { COINFT_V46_FRAME_CMD_START_FRAME = 0x1u };

typedef struct {
    uint16_t raw_count;   /* +0x00: the value transmitted on UART */
    uint16_t baseline;    /* +0x02: equals raw_count while idle */
    uint16_t field2;      /* +0x04: zero in every captured state */
    uint16_t field3;      /* +0x06: zero in every captured state */
    uint16_t limit_word;  /* +0x08: 0x1e00 / 0x3c00 / 0x2000 by group */
} coinft_v46_sensor_record_t;

_Static_assert(sizeof(coinft_v46_sensor_record_t) ==
               COINFT_V46_SENSOR_RECORD_STRIDE,
               "CoinFT sensor record must be 10 bytes");

/* Observed limit_word values.  The split is by channel group, not per sensor:
 * channels 0..7 share one value, channels 8..11 another, and record 12 a
 * third.  The business meaning of the field is still not proven, so it keeps a
 * descriptive name. */
enum {
    COINFT_V46_LIMIT_GROUP_LOW = 0x1E00u,
    COINFT_V46_LIMIT_GROUP_HIGH = 0x3C00u,
    COINFT_V46_LIMIT_INDEX12 = 0x2000u
};

static uint32_t coinft_v46_sensor_record_addr(unsigned index)
{
    return (uint32_t)COINFT_V46_SENSOR_RECORD_BASE +
           index * (uint32_t)COINFT_V46_SENSOR_RECORD_STRIDE;
}

static uint32_t coinft_v46_history_addr(unsigned index)
{
    return (uint32_t)COINFT_V46_HISTORY_BASE +
           index * (uint32_t)COINFT_V46_HISTORY_STRIDE;
}

/* The transmitted payload is field 0 of the first twelve records, in order.
 * This is the mapping the synchronized capture proves. */
static bool coinft_v46_uart_payload_from_records(
    const coinft_v46_sensor_record_t *records, size_t count,
    uint16_t *out, size_t out_count)
{
    size_t index;
    if (records == NULL || out == NULL ||
        count < (size_t)COINFT_V46_UART_CHANNEL_COUNT ||
        out_count != (size_t)COINFT_V46_UART_CHANNEL_COUNT)
        return false;
    for (index = 0u; index < (size_t)COINFT_V46_UART_CHANNEL_COUNT; ++index)
        out[index] = records[index].raw_count;
    return true;
}

/* Word i of the six-word sensor frame lands at these offsets; the block is
 * contiguous from SNS_SW_SEL_CSW_MASK2 to SNS_CTL. */
static bool coinft_v46_frame_register_offset(unsigned word_index,
                                             uint32_t *offset)
{
    if (offset == NULL || word_index >= 6u)
        return false;
    *offset = (uint32_t)COINFT_V46_MSC_SNS_SW_SEL_CSW_MASK2 + word_index * 4u;
    return true;
}

/* Only INTR.SCAN is unmasked on the live target (INTR_MASK == 0x100), so the
 * firmware's MSC interrupt is a scan-complete interrupt. */
static bool coinft_v46_scan_interrupt_enabled(uint32_t intr_mask)
{
    return (intr_mask & COINFT_V46_INTR_SCAN) != 0u &&
           (intr_mask & ~(uint32_t)COINFT_V46_INTR_SCAN) == 0u;
}

static bool coinft_v46_intr_pending(uint32_t intr, uint32_t intr_mask,
                                    uint32_t cause)
{
    return (intr & intr_mask & cause) != 0u;
}

static uint32_t coinft_v46_frame_cmd_start(void)
{
    return (uint32_t)COINFT_V46_FRAME_CMD_START_FRAME;
}

/* While the firmware is idle the baseline of every channel equals its raw
 * count; once streaming starts the baseline holds and the raw count drifts.
 * Expressed as a predicate so the host tests can assert both directions. */
static bool coinft_v46_idle_baseline_invariant(
    const coinft_v46_sensor_record_t *records, size_t count)
{
    size_t index;
    if (records == NULL || count == 0u)
        return false;
    for (index = 0u; index < count; ++index) {
        if (records[index].raw_count != records[index].baseline)
            return false;
    }
    return true;
}

/* A live SWD read of the record array races the firmware's own update of it.
 * Genuine tearing shows up as one contiguous run of disagreeing channels, so
 * the classifier below distinguishes "torn read" from "wrong address".  It is
 * an analysis aid, not firmware behaviour. */
static bool coinft_v46_mismatch_is_contiguous_run(const uint16_t *sram,
                                                  const uint16_t *uart,
                                                  size_t count)
{
    size_t index = 0u;
    size_t runs = 0u;
    if (sram == NULL || uart == NULL)
        return false;
    while (index < count) {
        if (sram[index] != uart[index]) {
            ++runs;
            while (index < count && sram[index] != uart[index])
                ++index;
        } else {
            ++index;
        }
    }
    return runs <= 1u;
}

/* ------------------------------------------------------------------------
 * v47: what a controlled external force does to the sensor records.
 *
 * v46 proved record[i].raw_count is UART channel i.  It could not say which
 * physical electrode channel i belongs to, nor what field2/field3 are for,
 * because every capture was taken on an unloaded sensor.  v47 adds a
 * quadrant-by-quadrant press experiment and settles all three.
 * ---------------------------------------------------------------------- */

/* Electrode identity per channel, from the firmware's widget pin records and
 * confirmed independently by the force experiment: pressing a quadrant drives
 * exactly the channels this table assigns to it. */
typedef enum {
    COINFT_V47_QUADRANT_RU = 0,
    COINFT_V47_QUADRANT_LU,
    COINFT_V47_QUADRANT_LD,
    COINFT_V47_QUADRANT_RD
} coinft_v47_quadrant_t;

static const coinft_v47_quadrant_t k_coinft_v47_channel_quadrant[12] = {
    COINFT_V47_QUADRANT_RU, /* ch0  RU1 P3.5 */
    COINFT_V47_QUADRANT_RU, /* ch1  RU2 P3.4 */
    COINFT_V47_QUADRANT_LD, /* ch2  LD2 P0.2 */
    COINFT_V47_QUADRANT_LD, /* ch3  LD1 P0.3 */
    COINFT_V47_QUADRANT_LU, /* ch4  LU2 P0.0 */
    COINFT_V47_QUADRANT_LU, /* ch5  LU1 P0.1 */
    COINFT_V47_QUADRANT_RD, /* ch6  RD2 P3.0 */
    COINFT_V47_QUADRANT_RD, /* ch7  RD1 P3.1 */
    COINFT_V47_QUADRANT_RU, /* ch8  RU1 P3.5, second CDAC configuration */
    COINFT_V47_QUADRANT_LD, /* ch9  LD2 P0.2, second CDAC configuration */
    COINFT_V47_QUADRANT_LU, /* ch10 LU2 P0.0, second CDAC configuration */
    COINFT_V47_QUADRANT_RD  /* ch11 RD2 P3.0, second CDAC configuration */
};

static bool coinft_v47_channel_quadrant(unsigned channel,
                                        coinft_v47_quadrant_t *quadrant)
{
    if (quadrant == NULL || channel >= 12u)
        return false;
    *quadrant = k_coinft_v47_channel_quadrant[channel];
    return true;
}

/* Channels 8..11 share an MCU pin with 0/2/4/6 and differ in exactly one
 * widget-context byte: cdac_sel_re at +0x2e is 40 instead of 20.  The sensor
 * record's limit_word differs by the same factor of two.  They are therefore a
 * second measurement configuration of the same electrode, not a copy. */
enum {
    COINFT_V47_CDAC_SEL_RE_PRIMARY = 20,
    COINFT_V47_CDAC_SEL_RE_PARTNER = 40
};

static bool coinft_v47_channel_pin_partner(unsigned channel, unsigned *partner)
{
    static const int table[12] = {8, -1, 9, -1, 10, -1, 11, -1, 0, 2, 4, 6};
    if (partner == NULL || channel >= 12u || table[channel] < 0)
        return false;
    *partner = (unsigned)table[channel];
    return true;
}

/* Force raises the raw count: every channel's steady-state raw exceeded its
 * unloaded value under load, and returned to within noise of the baseline on
 * release. */
static bool coinft_v47_force_increases_raw(uint16_t unloaded_raw,
                                           uint16_t loaded_raw)
{
    return loaded_raw > unloaded_raw;
}

/* The baseline does not follow raw upward: across a five-minute session with
 * 30 s holds on each quadrant, all 13 records kept exactly one distinct
 * baseline value while raw rose by ~3000.  A later direct test showed it also
 * does NOT reload on an i->s transition, and that idle does not force
 * raw == baseline, so this predicate asserts only the observed invariant
 * (constant under load) and not any particular update rule. */
static bool coinft_v47_baseline_frozen(const uint16_t *baseline_samples,
                                       size_t count)
{
    size_t index;
    if (baseline_samples == NULL || count == 0u)
        return false;
    for (index = 1u; index < count; ++index) {
        if (baseline_samples[index] != baseline_samples[0])
            return false;
    }
    return true;
}

/* field2/field3 stayed zero in 111644 record samples spanning unloaded, loaded
 * and released states.  They are therefore not the CapSense diff/touch-status
 * pair they were provisionally suspected to be; nothing in the observed
 * streaming path ever writes them. */
static bool coinft_v47_unused_fields_zero(
    const coinft_v46_sensor_record_t *record)
{
    return record != NULL && record->field2 == 0u && record->field3 == 0u;
}

/* Signed force signal relative to the frozen baseline. */
static int32_t coinft_v47_force_signal(
    const coinft_v46_sensor_record_t *record)
{
    if (record == NULL)
        return 0;
    return (int32_t)record->raw_count - (int32_t)record->baseline;
}

/* ------------------------------------------------------------------------
 * v48: the UART command protocol, consolidated.
 *
 * The machine-code behaviour of every command was already recovered in v2 and
 * tabulated in v33, but v45-v47 rewrote their "unresolved" lists and demoted it
 * back to "protocol unknown".  Acting on that demotion, v48 probed 't' live and
 * stalled the firmware.  The model below is the durable form of that knowledge;
 * 07_analysis/fact_registry_v48.json is its machine-checked index.
 * ---------------------------------------------------------------------- */

enum {
    COINFT_V48_CMD_QUERY = 'q',
    COINFT_V48_CMD_STREAM = 's',
    COINFT_V48_CMD_IDLE = 'i',
    COINFT_V48_CMD_SET_CHANNEL = 't',
    COINFT_V48_CMD_MAILBOX = 'w',
    COINFT_V48_CMD_TERMINATE = 'n'
};

/* 't' is the only command that takes parameters, and it reads them with
 * blocking UART reads.  Sending the bare character stalls the main loop: scan
 * re-arming and UART frame packing both stop until four more bytes arrive, and
 * whatever bytes arrive next are consumed as parameters rather than commands. */
enum { COINFT_V48_CMD_SET_CHANNEL_PARAM_BYTES = 4 };

typedef struct {
    uint8_t index;      /* channel index; ignored unless < 12 */
    uint8_t value_lo;
    uint8_t value_hi;
    uint8_t extra;      /* read and discarded by the firmware */
} coinft_v48_set_channel_args_t;

_Static_assert(sizeof(coinft_v48_set_channel_args_t) ==
               COINFT_V48_CMD_SET_CHANNEL_PARAM_BYTES,
               "'t' consumes exactly four parameter bytes");

/* Total bytes a host must send for one command to complete without stalling. */
static size_t coinft_v48_command_length(uint8_t command)
{
    return command == COINFT_V48_CMD_SET_CHANNEL
        ? 1u + (size_t)COINFT_V48_CMD_SET_CHANNEL_PARAM_BYTES
        : 1u;
}

static bool coinft_v48_command_stalls_main_loop(uint8_t command,
                                               size_t bytes_supplied)
{
    return bytes_supplied < coinft_v48_command_length(command);
}

/* Only indices below the channel count take effect; 0xff is a safe no-op that
 * still satisfies the blocking reads, which is how v48 recovered the stalled
 * firmware without a power cycle. */
static bool coinft_v48_set_channel_applies(
    const coinft_v48_set_channel_args_t *args)
{
    return args != NULL && args->index < (uint8_t)COINFT_V46_UART_CHANNEL_COUNT;
}

static uint16_t coinft_v48_set_channel_value(
    const coinft_v48_set_channel_args_t *args)
{
    if (args == NULL)
        return 0u;
    return (uint16_t)((uint16_t)args->value_lo |
                      ((uint16_t)args->value_hi << 8));
}

/* Scanning and UART output are separable: after scanning stops the firmware
 * keeps transmitting the last record contents, so a live byte stream is not
 * evidence that the data behind it is fresh.  This corrects v46's shorthand
 * that INTR != 0 is equivalent to streaming. */
typedef struct {
    uint32_t msc_intr;
    bool uart_bytes_advancing;
} coinft_v48_activity_t;

static bool coinft_v48_scanning(const coinft_v48_activity_t *activity)
{
    return activity != NULL && activity->msc_intr != 0u;
}

static bool coinft_v48_transmitting_stale_data(
    const coinft_v48_activity_t *activity)
{
    return activity != NULL && activity->uart_bytes_advancing &&
           activity->msc_intr == 0u;
}

/* ------------------------------------------------------------------------
 * v49 stage F: identify what the remaining functions actually are.
 *
 * Two labels that survived seven rounds were wrong, and both cost real time:
 * FUN_00004378 was called a clock-conversion routine (v42) and FUN_00004484 a
 * CapSense mode-0 validity callback whose "business meaning" was listed as
 * unresolved from v44 to v47.  Both are ARM EABI integer-division runtime
 * helpers.  Nothing about them needs to be modelled; they needed to be named.
 * ---------------------------------------------------------------------- */

/* Every division in this firmware is a call to the helper, which is why the
 * decompiler output contains no `/` or `%` operator anywhere. */
enum {
    COINFT_V49_HELPER_UIDIVMOD = 0x00004378, /* __aeabi_uidivmod / __udivsi3 */
    COINFT_V49_HELPER_DIVMOD_GUARD = 0x00004484, /* divisor!=0 ? div : idiv0 */
    COINFT_V49_HELPER_IDIV0 = 0x0000448C,    /* __aeabi_idiv0, empty */
    COINFT_V49_HELPER_MEMSET = 0x000044B8
};

/* The guard wrapper's only decision.  Modelled so that call sites which look
 * like "validity checks" can be read correctly: the predicate is about the
 * divisor being non-zero, not about any sensor or mode being valid. */
static bool coinft_v49_divmod_guard_divides(uint32_t divisor)
{
    return divisor != 0u;
}

/* CapSense enable, FUN_00000bf4.
 *
 * The loop bound is the strongest independent confirmation of the widget count:
 * it walks +0x23 with stride 0x38 until it reaches DAT_00000cd8 == 0x2fb, and
 * 0x23 + 13 * 0x38 == 0x2fb exactly.  v36 established that FUN_000027b0 reads
 * this same byte and tests (b & 6) == 6, so this function is what arms it. */
enum {
    COINFT_V49_WIDGET_ENABLE_OFFSET = 0x23,
    COINFT_V49_WIDGET_ENABLE_BITS = 0x6,
    COINFT_V49_WIDGET_ENABLE_LOOP_END = 0x2FB
};

_Static_assert(COINFT_V49_WIDGET_ENABLE_OFFSET +
               COINFT_V46_SENSOR_RECORD_COUNT * 0x38 ==
               COINFT_V49_WIDGET_ENABLE_LOOP_END,
               "enable loop must cover exactly 13 widget contexts");

/* Returns how many widgets the loop arms, given the firmware's own bound. */
static unsigned coinft_v49_enable_all_widgets(uint8_t *context_base,
                                              uint32_t loop_end)
{
    unsigned offset = COINFT_V49_WIDGET_ENABLE_OFFSET;
    unsigned armed = 0u;
    if (context_base == NULL)
        return 0u;
    while (offset != loop_end) {
        context_base[offset] |= (uint8_t)COINFT_V49_WIDGET_ENABLE_BITS;
        offset += 0x38u;
        ++armed;
    }
    return armed;
}

static bool coinft_v49_widget_armed(uint8_t enable_byte)
{
    /* Matches FUN_000027b0's test, not merely "non-zero". */
    return (enable_byte & COINFT_V49_WIDGET_ENABLE_BITS) ==
           COINFT_V49_WIDGET_ENABLE_BITS;
}

/* SCB UART init, FUN_000038e4.  The peripheral base arrives as a parameter,
 * which is why address-literal scans classified it as pure logic. */
enum {
    COINFT_V49_SCB_CTRL = 0x00,
    COINFT_V49_SCB_UART_CTRL = 0x40,
    COINFT_V49_SCB_UART_TX_CTRL = 0x44,
    COINFT_V49_SCB_UART_RX_CTRL = 0x48,
    COINFT_V49_SCB_MODE_POS = 24,
    COINFT_V49_SCB_MODE_UART = 2
};

static uint32_t coinft_v49_scb_ctrl_mode(uint32_t ctrl)
{
    return (ctrl >> COINFT_V49_SCB_MODE_POS) & 0x3u;
}

static bool coinft_v49_scb_configured_as_uart(uint32_t ctrl)
{
    return coinft_v49_scb_ctrl_mode(ctrl) ==
           (uint32_t)COINFT_V49_SCB_MODE_UART;
}

/* TCPWM counter init, FUN_00004274: CNT[n] blocks are 0x40 bytes from 0x100. */
enum {
    COINFT_V49_TCPWM_CNT_BASE = 0x100,
    COINFT_V49_TCPWM_CNT_STRIDE = 0x40,
    COINFT_V49_TCPWM_CNT_CTRL = 0x00,
    COINFT_V49_TCPWM_CNT_COUNTER = 0x08,
    COINFT_V49_TCPWM_CNT_CC = 0x0C,
    COINFT_V49_TCPWM_CNT_CC_BUFF = 0x10,
    COINFT_V49_TCPWM_CNT_PERIOD = 0x14,
    COINFT_V49_TCPWM_CNT_TR_CTRL0 = 0x20
};

static uint32_t coinft_v49_tcpwm_reg(unsigned counter, unsigned reg_offset)
{
    return (uint32_t)COINFT_V49_TCPWM_CNT_BASE +
           counter * (uint32_t)COINFT_V49_TCPWM_CNT_STRIDE + reg_offset;
}

/* ------------------------------------------------------------------------
 * v50 stage G: the storage path is emulated EEPROM, not a message layer.
 *
 * v36 listed FUN_00002E14 as "an upper-layer message object, mapping to the
 * UART t/w parameters".  It is Cy_Em_EEPROM_Write: it shares its signature with
 * FUN_00002ba4 and the pair is used to persist a device ID string.
 * ---------------------------------------------------------------------- */

enum {
    COINFT_V50_EEPROM_READ = 0x00002BA4,
    COINFT_V50_EEPROM_WRITE = 0x00002E14,
    COINFT_V50_EEPROM_INIT = 0x00002A50,
    COINFT_V50_EEPROM_CRC8 = 0x00002870
};

/* Em_EEPROM status codes; the middleware's own 0x093E0000 base. */
enum {
    COINFT_V50_EEPROM_SUCCESS = 0x00000000u,
    COINFT_V50_EEPROM_BAD_PARAM = 0x093E0001u,
    COINFT_V50_EEPROM_WRITE_FAIL = 0x093E0004u
};

/* Runtime configuration read out of the context at 0x20000af0. */
enum {
    COINFT_V50_EEPROM_SIZE = 128,
    COINFT_V50_EEPROM_WEAR_LEVELLING = 2,
    COINFT_V50_EEPROM_REDUNDANT_COPY = 0,
    COINFT_V50_EEPROM_FLASH_BASE = 0x00005000,
    COINFT_V50_EEPROM_BLOCK_LIST = 0x00005100,
    COINFT_V50_DEVICE_ID_ADDR = 0x40,
    COINFT_V50_DEVICE_ID_LEN = 12,
    COINFT_V50_DEVICE_ID_TEMPLATE = 0x00004F78
};

/* CRC-8, polynomial 0x31, initial value 0xFF.  The firmware checksums
 * (length - 4) bytes starting one byte into the row. */
static uint8_t coinft_v50_eeprom_crc8(const uint8_t *row, size_t length)
{
    uint8_t crc = 0xFFu;
    size_t i;
    unsigned bit;
    if (row == NULL || length < 4u)
        return crc;
    for (i = 0u; i < length - 4u; ++i) {
        crc ^= row[1u + i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                                : (uint8_t)(crc << 1);
    }
    return crc;
}

/* A row whose stored checksum disagrees is not returned to the caller: the
 * read path memsets the destination instead, so corrupt storage yields zeros
 * rather than stale bytes. */
static bool coinft_v50_row_valid(const uint8_t *row, size_t length,
                                 uint8_t stored_crc)
{
    return coinft_v50_eeprom_crc8(row, length) == stored_crc;
}

/* Wear-levelling copy selection, FUN_000028de. */
static uint32_t coinft_v50_select_copy(uint32_t pointer, uint32_t base,
                                       uint32_t unit, unsigned factor)
{
    if (factor <= 1u)
        return pointer;
    return (pointer < base + unit) ? ((factor - 1u) * unit + pointer)
                                   : (pointer - unit);
}

/* Device-ID bootstrap, FUN_000005f8: read 12 bytes from logical address 0x40;
 * if no CR terminator is present the row is treated as uninitialised, the
 * default template is copied in, and the result is written back. */
static bool coinft_v50_device_id_initialised(const uint8_t *buffer,
                                             size_t length)
{
    size_t i;
    if (buffer == NULL)
        return false;
    for (i = 0u; i < length; ++i) {
        if (buffer[i] == (uint8_t)'\r')
            return true;
    }
    return false;
}

/* ------------------------------------------------------------------------
 * v51 stage H (static half): the command table is now complete.
 *
 * Two commands beyond 't' block the main loop, and two can write flash.  That
 * combination is why probing them live requires a plan rather than curiosity:
 * v48 stalled the firmware by sending a bare 't', and 'w' behaves the same way
 * while additionally committing an Em_EEPROM write.
 * ---------------------------------------------------------------------- */

/* SCB register offsets used by the console, from the firmware's own accesses
 * (base 0x40270000, indexed as uint32 in the decompiler output). */
enum {
    COINFT_V51_SCB_BASE = 0x40270000,
    COINFT_V51_SCB_TX_FIFO_STATUS = 0x208, /* param_1[0x82] */
    COINFT_V51_SCB_TX_FIFO_WR = 0x240,     /* param_1[0x90] */
    COINFT_V51_SCB_RX_FIFO_RD = 0x340,     /* param_1[0xd0] */
    COINFT_V51_RX_EMPTY = 0xFFFFFFFFu      /* polled until it changes */
};

/* 'q' replies with this single byte. */
enum { COINFT_V51_QUERY_REPLY = 0x1A };

/* 'w' reads until CR or this many bytes, whichever comes first. */
enum {
    COINFT_V51_WRITE_ID_MAX = 12,
    COINFT_V51_CR = 0x0D
};

typedef enum {
    COINFT_V51_EFFECT_NONE = 0,
    COINFT_V51_EFFECT_BLOCKS = 1 << 0,      /* stalls the main loop */
    COINFT_V51_EFFECT_FLASH_WRITE = 1 << 1, /* commits an Em_EEPROM write */
    COINFT_V51_EFFECT_SRAM_CONFIG = 1 << 2  /* changes runtime configuration */
} coinft_v51_effect_t;

/* What each command costs, so a caller can decide before sending. */
static unsigned coinft_v51_command_effects(uint8_t command)
{
    switch (command) {
    case 'q':
        return COINFT_V51_EFFECT_NONE;
    case 's':
    case 'i':
        return COINFT_V51_EFFECT_NONE;
    case 't':
        /* Blocks for four parameter bytes, then rewrites a channel's scan
         * parameter in SRAM; i/s do not undo it. */
        return COINFT_V51_EFFECT_BLOCKS | COINFT_V51_EFFECT_SRAM_CONFIG;
    case 'w':
        /* Blocks for up to 12 bytes or a CR, writes the device ID to flash,
         * then falls through into 'n'. */
        return COINFT_V51_EFFECT_BLOCKS | COINFT_V51_EFFECT_FLASH_WRITE;
    case 'n':
        /* Re-reads the device ID; if the row is uninitialised the bootstrap
         * writes the default template back, so this can also touch flash. */
        return COINFT_V51_EFFECT_FLASH_WRITE;
    default:
        return COINFT_V51_EFFECT_NONE;
    }
}

static bool coinft_v51_command_is_read_only(uint8_t command)
{
    return coinft_v51_command_effects(command) == COINFT_V51_EFFECT_NONE;
}

/* How many bytes 'w' consumes for a given payload: the CR terminates early. */
static size_t coinft_v51_write_id_length(const uint8_t *payload, size_t length)
{
    size_t i;
    if (payload == NULL)
        return 1u;
    for (i = 0u; i < length && i < (size_t)COINFT_V51_WRITE_ID_MAX; ++i) {
        if (payload[i] == (uint8_t)COINFT_V51_CR)
            return 1u + i + 1u;
    }
    return 1u + (size_t)COINFT_V51_WRITE_ID_MAX;
}

/* 'w' falls through to 'n' with no break, so a write is always followed by a
 * read-back in the same command dispatch. */
static bool coinft_v51_write_falls_through_to_read(uint8_t command)
{
    return command == 'w';
}

/* ------------------------------------------------------------------------
 * v51 stage I: the baseline update rule, and the Em_EEPROM row layout as it
 * actually appears in flash.
 * ---------------------------------------------------------------------- */

/* The baseline is never updated autonomously.  20 minutes of idle streaming
 * (21216 reads) produced zero baseline changes, and raw never fell below
 * baseline in any channel, so the "downward tracking" hypothesis was never even
 * exercised -- and is unnecessary, because every baseline change observed
 * across earlier sessions is explained by the tare below. */
static bool coinft_v51_baseline_changes_autonomously(void)
{
    return false;
}

/* 't' performs the tare unconditionally: an index rejected by the `index < 12`
 * test still re-initialises every record's baseline.  So the command has two
 * independent effects, which is why v48 -- always writing a value that moved
 * raw at the same time -- could not see the tare. */
static bool coinft_v51_tare_applies(uint8_t index)
{
    (void)index;
    return true;
}

static bool coinft_v51_scan_parameter_applies(uint8_t index)
{
    return index < (uint8_t)COINFT_V46_UART_CHANNEL_COUNT;
}

/* After a tare, every channel's raw sits within a few counts of its baseline;
 * the residual is one scan cycle of noise, measured at sigma = 4.7 counts. */
enum { COINFT_V51_TARE_RESIDUAL_LIMIT = 16 };

static bool coinft_v51_tare_residual_ok(int32_t raw_minus_baseline)
{
    return raw_minus_baseline <= COINFT_V51_TARE_RESIDUAL_LIMIT &&
           raw_minus_baseline >= -COINFT_V51_TARE_RESIDUAL_LIMIT;
}

/* The pending-write block that v50 derived from the decompiler's second pass
 * was confirmed byte-for-byte in flash after a live 'w': writing the 7-byte
 * payload "CFT-A1\r" to logical address 0x40 produced address 0x40 at +0x08,
 * length 0x0c at +0x0c and the payload from +0x10. */
enum {
    COINFT_V51_BLOCK_CRC = 0x00,
    COINFT_V51_BLOCK_ADDRESS = 0x08,
    COINFT_V51_BLOCK_LENGTH = 0x0C,
    COINFT_V51_BLOCK_DATA = 0x10,
    /* Main storage row for logical address 0x40. */
    COINFT_V51_ID_ROW_FLASH = 0x000050C0
};

static bool coinft_v51_block_covers(uint32_t block_addr, uint32_t block_len,
                                   uint32_t want_addr, uint32_t want_len)
{
    return block_addr < want_addr + want_len &&
           want_addr < block_addr + block_len;
}
