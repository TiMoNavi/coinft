#include "cycfg_capsense.h"
#include "factory_frame_table.h"

#include "cy_sysclk.h"

#include <stddef.h>

#define COINFT_MSC_CHANNELS  (2u)

_Static_assert(sizeof(cy_stc_capsense_sensor_context_t) == 10u,
               "factory sensor context ABI changed");
_Static_assert(offsetof(cy_stc_capsense_sensor_context_t, cdacComp) == 9u,
               "compensation CDAC field moved");
_Static_assert(sizeof(cy_stc_capsense_widget_context_t) == 72u,
               "CapSense 10 widget context ABI changed");
_Static_assert(offsetof(cy_stc_capsense_widget_context_t, cdacRef) == 58u,
               "reference CDAC field moved");
_Static_assert(sizeof(cy_stc_capsense_widget_config_t) == 140u,
               "factory widget configuration ABI changed");
_Static_assert(sizeof(cy_stc_msc_base_config_t) == 396u,
               "factory MSC base configuration ABI changed");
_Static_assert(sizeof(coinft_factory_frames) == (14u * 6u * sizeof(uint32_t)),
               "factory frame table size changed");

static cy_stc_msc_context_t msc_context[COINFT_MSC_CHANNELS];
static cy_stc_capsense_internal_context_t internal_context;
static cy_stc_capsense_common_context_t common_context;
static cy_stc_capsense_active_scan_sns_t active_sensor[COINFT_MSC_CHANNELS];
static cy_stc_msc_base_config_t base_frames[COINFT_MSC_CHANNELS];
static uint32_t sensor_frames[COINFT_FACTORY_FRAME_COUNT * COINFT_FACTORY_WORDS_PER_FRAME];
static uint8_t debounce[COINFT_WIDGET_COUNT];

/* Widget ID to physical sensor-frame index in the 2 x 7-slot table. */
static const uint8_t output_frame_index[COINFT_OUTPUT_CHANNEL_COUNT] = {
    0u, 1u, 7u, 8u, 9u, 10u, 2u, 3u, 4u, 11u, 12u, 5u,
};

/* Physical pins are repeated for the four ganged, second-sensitivity widgets.
 * The repetition is intentional: each electrode owns a contiguous pin list. */
static const cy_stc_capsense_pin_config_t sensor_pins[CY_CAPSENSE_PIN_COUNT] = {
    {GPIO_PRT3, 5u, 4u, 0u}, /* w0  */
    {GPIO_PRT3, 4u, 3u, 0u}, /* w1  */
    {GPIO_PRT0, 2u, 6u, 1u}, /* w2  */
    {GPIO_PRT0, 3u, 7u, 1u}, /* w3  */
    {GPIO_PRT0, 0u, 4u, 1u}, /* w4  */
    {GPIO_PRT0, 1u, 5u, 1u}, /* w5  */
    {GPIO_PRT3, 0u, 1u, 0u}, /* w6  */
    {GPIO_PRT3, 1u, 2u, 0u}, /* w7  */
    {GPIO_PRT3, 5u, 4u, 0u}, /* w8, pin 0 */
    {GPIO_PRT3, 4u, 3u, 0u}, /* w8, pin 1 */
    {GPIO_PRT0, 2u, 6u, 1u}, /* w9, pin 0 */
    {GPIO_PRT0, 3u, 7u, 1u}, /* w9, pin 1 */
    {GPIO_PRT0, 0u, 4u, 1u}, /* w10, pin 0 */
    {GPIO_PRT0, 1u, 5u, 1u}, /* w10, pin 1 */
    {GPIO_PRT3, 0u, 1u, 0u}, /* w11, pin 0 */
    {GPIO_PRT3, 1u, 2u, 0u}, /* w11, pin 1 */
    {GPIO_PRT3, 7u, 6u, 0u}, /* w12 reference */
};

static const cy_stc_capsense_pin_config_t shield_pin = {
    GPIO_PRT3, 6u, 5u, 0u
};

static const cy_stc_capsense_electrode_config_t electrodes[12] = {
    {&sensor_pins[0],  0u, 1u, 0u},
    {&sensor_pins[1],  0u, 1u, 0u},
    {&sensor_pins[2],  0u, 1u, 1u},
    {&sensor_pins[3],  0u, 1u, 1u},
    {&sensor_pins[4],  0u, 1u, 1u},
    {&sensor_pins[5],  0u, 1u, 1u},
    {&sensor_pins[6],  0u, 1u, 0u},
    {&sensor_pins[7],  0u, 1u, 0u},
    {&sensor_pins[8],  0u, 2u, 0u},
    {&sensor_pins[10], 0u, 2u, 1u},
    {&sensor_pins[12], 0u, 2u, 1u},
    {&sensor_pins[14], 0u, 2u, 0u},
};

const cy_stc_capsense_electrode_config_t coinft_reference_electrode = {
    &sensor_pins[16], 0u, 1u, 0u
};

static const cy_stc_capsense_electrode_config_t shield_electrode = {
    &shield_pin, 0u, 1u, 0u
};

static cy_stc_capsense_channel_config_t channels[COINFT_MSC_CHANNELS] = {
    {
        .ptrHwBase = MSC0,
        .ptrHwContext = &msc_context[0],
        .portCmod1 = GPIO_PRT4,
        .pinCmod1 = 0u,
        .portCmod2 = GPIO_PRT4,
        .pinCmod2 = 1u,
        .ptrShieldEltdConfig = &shield_electrode,
    },
    {
        .ptrHwBase = MSC1,
        .ptrHwContext = &msc_context[1],
        .portCmod1 = GPIO_PRT7,
        .pinCmod1 = 0u,
        .portCmod2 = GPIO_PRT7,
        .pinCmod2 = 1u,
        .ptrShieldEltdConfig = &shield_electrode,
    },
};

/* Seven slots per MSC. MSC1 slot 6 is shield-only and redirects to MSC0
 * slot 6 through snsId=0, matching the factory configuration. */
static const cy_stc_capsense_scan_slot_t scan_slots[14] = {
    { 0u, 0u}, { 1u, 0u}, { 6u, 0u}, { 7u, 0u},
    { 8u, 0u}, {11u, 0u}, {12u, 0u},
    { 2u, 0u}, { 3u, 0u}, { 4u, 0u}, { 5u, 0u},
    { 9u, 0u}, {10u, 0u}, {CY_CAPSENSE_SLOT_SHIELD_ONLY, 0u},
};

#define SENSOR_CONTEXT(comp_) \
    {.raw = 0u, .bsln = 0u, .diff = 0u, .status = 0u, \
     .negBslnRstCnt = 0u, .bslnExt = 0u, .cdacComp = (comp_)}

static cy_stc_capsense_sensor_context_t sensor_context[COINFT_WIDGET_COUNT] = {
    SENSOR_CONTEXT(30u), SENSOR_CONTEXT(30u), SENSOR_CONTEXT(30u),
    SENSOR_CONTEXT(30u), SENSOR_CONTEXT(30u), SENSOR_CONTEXT(30u),
    SENSOR_CONTEXT(30u), SENSOR_CONTEXT(30u),
    SENSOR_CONTEXT(60u), SENSOR_CONTEXT(60u), SENSOR_CONTEXT(60u),
    SENSOR_CONTEXT(60u), SENSOR_CONTEXT(32u),
};

#define WIDGET_CONTEXT(sub_, clk_, ref_, comp_div_, max_) \
    { \
        .fingerCap = 160u, .maxRawCount = (max_), .maxRawCountRow = (max_), \
        .fingerTh = 100u, .proxTh = 100u, .lowBslnRst = 30u, \
        .snsClk = (clk_), .rowSnsClk = (clk_), .noiseTh = 40u, \
        .nNoiseTh = 40u, .hysteresis = 10u, .onDebounce = 3u, \
        .snsClkSource = CY_CAPSENSE_CLK_SOURCE_PRS, .bslnCoeff = 1u, \
        .numSubConversions = (sub_), .cdacRef = (ref_), .rowCdacRef = (ref_), \
        .cicRate = 1u, .cdacDitherEn = 0u, .cdacDitherValue = 0u, \
        .coarseInitBypassEn = 0u, .cdacCompDivider = (comp_div_), \
        .lfsrBits = CY_CAPSENSE_LFSR_BITS_AUTO, .reserved0 = 0u \
    }

static cy_stc_capsense_widget_context_t widget_context[COINFT_WIDGET_COUNT] = {
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 20u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 40u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 40u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 40u,  4u, 32760u),
    WIDGET_CONTEXT(1024u, 16u, 40u,  4u, 32760u),
    WIDGET_CONTEXT( 128u, 16u, 32u, 16u,  2048u),
};

#define WIDGET_CONFIG(id_, first_, electrode_) \
    { \
        .ptrWdContext = &widget_context[(id_)], \
        .ptrSnsContext = &sensor_context[(id_)], \
        .ptrEltdConfig = (electrode_), \
        .iirCoeff = 128u, .ptrDebounceArr = &debounce[(id_)], \
        .numSns = 1u, .numCols = 1u, .numRows = 0u, \
        .firstSlotId = (first_), .numSlots = 1u, \
        .senseMethod = CY_CAPSENSE_CSD_GROUP, \
        .wdType = (uint8_t)CY_CAPSENSE_WD_BUTTON_E, \
        .numChopCycles = 1u, .mpOrder = 1u, \
        .lfsrDitherLimit = 10u, \
        .snsClkSourceAutoSelMode = CY_CAPSENSE_SNS_CLK_SOURCE_AUTO_SEL_MODE_STRONG, \
        .mfsConfig = 0u \
    }

static const cy_stc_capsense_widget_config_t widget_config[COINFT_WIDGET_COUNT] = {
    WIDGET_CONFIG( 0u, 0u, &electrodes[0]),
    WIDGET_CONFIG( 1u, 1u, &electrodes[1]),
    WIDGET_CONFIG( 2u, 0u, &electrodes[2]),
    WIDGET_CONFIG( 3u, 1u, &electrodes[3]),
    WIDGET_CONFIG( 4u, 2u, &electrodes[4]),
    WIDGET_CONFIG( 5u, 3u, &electrodes[5]),
    WIDGET_CONFIG( 6u, 2u, &electrodes[6]),
    WIDGET_CONFIG( 7u, 3u, &electrodes[7]),
    WIDGET_CONFIG( 8u, 4u, &electrodes[8]),
    WIDGET_CONFIG( 9u, 4u, &electrodes[9]),
    WIDGET_CONFIG(10u, 5u, &electrodes[10]),
    WIDGET_CONFIG(11u, 5u, &electrodes[11]),
    WIDGET_CONFIG(12u, 6u, &coinft_reference_electrode),
};

static const cy_stc_capsense_common_config_t common_config = {
    .cpuClkHz = 48000000u,
    .periClkHz = 48000000u,
    .ptrChConfig = channels,
    .numPin = CY_CAPSENSE_PIN_COUNT,
    .numSns = CY_CAPSENSE_SENSOR_COUNT,
    .numWd = CY_CAPSENSE_WIDGET_COUNT,
    .periDividerType = (uint8_t)CY_SYSCLK_DIV_16_BIT,
    .periDividerIndex = 0u,
    .analogWakeupDelay = 10u,
    .proxTouchCoeff = 1000u,
    .swSensorAutoResetEn = CY_CAPSENSE_DISABLE,
    .csdInactiveSnsConnection = CY_CAPSENSE_SNS_CONNECTION_SHIELD,
    .csxInactiveSnsConnection = CY_CAPSENSE_SNS_CONNECTION_GROUND,
    .isxInactiveSnsConnection = CY_CAPSENSE_SNS_CONNECTION_HIGHZ,
    .csdShieldNumPin = CY_CAPSENSE_SHIELD_PIN_COUNT,
    .csdRawTarget = 85u,
    .csdCalibrationError = 10u,
    .csxRawTarget = 40u,
    .csxCalibrationError = 20u,
    .isxRawTarget = 40u,
    .isxCalibrationError = 20u,
    .csdShieldMode = CY_CAPSENSE_SHIELD_ACTIVE,
    .sensorConnection = CY_CAPSENSE_CTRLMUX_SENSOR_CONNECTION_METHOD,
    .numProOffsetCycles = 0u,
    .proOffsetCdacComp = 0u,
    .numEpiCycles = 120u,
    .chopPolarity = CY_CAPSENSE_DISABLE,
    .numCoarseInitChargeCycles = 200u,
    .numCoarseInitSettleCycles = 200u,
    .numBadScans = 0u,
    .counterMode = CY_CAPSENSE_COUNTER_MODE_SATURATE,
    .syncFrameStartEn = CY_CAPSENSE_DISABLE,
    .syncClockEn = CY_CAPSENSE_DISABLE,
    .syncMode = CY_CAPSENSE_SYNC_INTERNAL,
    .masterChannelId = 0u,
    .numChannels = COINFT_MSC_CHANNELS,
    .numSlots = CY_CAPSENSE_SLOT_COUNT,
    .channelOffset = 0u,
};

cy_stc_capsense_context_t cy_capsense_context = {
    .ptrCommonConfig = &common_config,
    .ptrCommonContext = &common_context,
    .ptrInternalContext = &internal_context,
    .ptrWdConfig = widget_config,
    .ptrWdContext = widget_context,
    .ptrPinConfig = sensor_pins,
    .ptrShieldPinConfig = &shield_pin,
    .ptrActiveScanSns = active_sensor,
    .ptrBistContext = NULL,
    .ptrBaseFrameContext = base_frames,
    .ptrSensorFrameContext = sensor_frames,
    .ptrScanSlots = scan_slots,
};

void coinft_capsense_load_factory_frames(void)
{
    for (uint32_t frame = 0u; frame < COINFT_FACTORY_FRAME_COUNT; ++frame) {
        for (uint32_t word = 0u; word < COINFT_FACTORY_WORDS_PER_FRAME; ++word) {
            sensor_frames[frame * COINFT_FACTORY_WORDS_PER_FRAME + word] =
                coinft_factory_frames[frame][word];
        }
    }

    /* Force the next ScanSlots call to submit the replaced frame data. */
    internal_context.currentSlotIndex = CY_CAPSENSE_SLOT_COUNT_MAX_VALUE;
}

bool coinft_capsense_apply_parameters(uint32_t channel,
                                      uint8_t cdac_ref,
                                      uint8_t cdac_comp)
{
    if (channel >= COINFT_OUTPUT_CHANNEL_COUNT) {
        return false;
    }

    widget_context[channel].cdacRef = cdac_ref;
    sensor_context[channel].cdacComp = cdac_comp;
    return true;
}

void coinft_capsense_prepare_frames(void)
{
    coinft_capsense_load_factory_frames();

    /* The factory table is the known-good output of the original generator.
     * Patch only the two fields changed by command 't'. */
    for (uint32_t channel = 0u;
         channel < COINFT_OUTPUT_CHANNEL_COUNT;
         ++channel) {
        const uint32_t frame = output_frame_index[channel];
        uint32_t *const cdac_ctl =
            &sensor_frames[frame * COINFT_FACTORY_WORDS_PER_FRAME + 4u];

        *cdac_ctl =
            (*cdac_ctl & ~(MSC_SNS_CDAC_CTL_SEL_RE_Msk |
                           MSC_SNS_CDAC_CTL_SEL_CO_Msk)) |
            ((uint32_t)widget_context[channel].cdacRef <<
             MSC_SNS_CDAC_CTL_SEL_RE_Pos) |
            ((uint32_t)sensor_context[channel].cdacComp <<
             MSC_SNS_CDAC_CTL_SEL_CO_Pos);
    }

    internal_context.currentSlotIndex = CY_CAPSENSE_SLOT_COUNT_MAX_VALUE;
}

uint16_t coinft_capsense_raw(uint32_t channel)
{
    return sensor_context[channel].raw;
}
