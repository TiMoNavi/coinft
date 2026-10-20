#include "cycfg_capsense.h"
#include "mlp_fixed.h"

#include "cy_device_headers.h"
#include "cy_em_eeprom.h"
#include "cy_gpio.h"
#include "cy_scb_common.h"
#include "cy_scb_uart.h"
#include "cy_sysclk.h"
#include "cy_syslib.h"
#include "cy_tcpwm.h"
#include "cy_tcpwm_counter.h"

#include <stdbool.h>
#include <stdint.h>

#define UART_FRAME_BYTES        (26u)
#define OUTPUT_CHANNELS         (12u)
#define SCAN_PARAMETER_BYTES    (24u)
#define DEVICE_NAME_BYTES       (12u)
#define EEPROM_LOGICAL_BYTES    (128u)
#define EEPROM_PHYSICAL_BYTES   (512u)
#define EEPROM_FLASH_ADDRESS    (0x00005000u)
#define MLP_DIAGNOSTIC_BYTES    (114u)
#define MLP_DIAGNOSTIC_VERSION  (1u)

_Static_assert(sizeof(cy_stc_eeprom_config_t) == 12u,
               "factory Em_EEPROM configuration ABI changed");
_Static_assert(CY_EM_EEPROM_GET_PHYSICAL_SIZE(EEPROM_LOGICAL_BYTES, 0u, 2u, 0u) ==
                   EEPROM_PHYSICAL_BYTES,
               "factory Em_EEPROM storage size changed");

static cy_stc_scb_uart_context_t uart_context;
static cy_stc_eeprom_context_t eeprom_context;
static volatile bool capsense_ready;
static volatile bool streaming;
static volatile uint8_t scan_phase;
static volatile uint8_t indicator_count;
static uint8_t scan_parameters[SCAN_PARAMETER_BYTES];
static uint8_t device_name[DEVICE_NAME_BYTES];
static bool tare_valid;
static int32_t tare_offset[COINFT_MLP_INPUTS];
static uint16_t latest_raw[COINFT_MLP_INPUTS];
static int32_t latest_zeroed[COINFT_MLP_INPUTS];
static coinft_mlp_result_t latest_mlp;
static uint32_t latest_mlp_cycles;
static uint32_t inference_sequence;

#define SCAN_PARAMETER_A (&scan_parameters[0])
#define SCAN_PARAMETER_B (&scan_parameters[OUTPUT_CHANNELS])

/* PSoC 4 Em_EEPROM uses zero as its erased/empty value. Programming this
 * section also restores the same blank state as the factory HEX. */
__attribute__((section(".coinft_eeprom"), used, aligned(CY_FLASH_SIZEOF_ROW)))
const uint8_t coinft_eeprom_storage[EEPROM_PHYSICAL_BYTES] = {0u};

static const cy_stc_eeprom_config_t eeprom_config = {
    .eepromSize = EEPROM_LOGICAL_BYTES,
    .simpleMode = 0u,
    .wearLevelingFactor = 2u,
    .redundantCopy = 0u,
    .blockingWrite = 1u,
    .userFlashStartAddr = EEPROM_FLASH_ADDRESS,
};

static const uint8_t default_device_name[DEVICE_NAME_BYTES] = {
    (uint8_t)'N', (uint8_t)'F', (uint8_t)'T', 0x0Du,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
};

static const cy_stc_scb_uart_config_t uart_config = {
    .uartMode = CY_SCB_UART_STANDARD,
    .oversample = 8u,
    .dataWidth = 8u,
    .enableMsbFirst = false,
    .stopBits = CY_SCB_UART_STOP_BITS_1,
    .parity = CY_SCB_UART_PARITY_NONE,
    .enableInputFilter = false,
    .dropOnParityError = false,
    .dropOnFrameError = false,
    .enableMutliProcessorMode = false,
    .receiverAddress = 0u,
    .receiverAddressMask = 0u,
    .acceptAddrInFifo = false,
    .irdaInvertRx = false,
    .irdaEnableLowPowerReceiver = false,
    .smartCardRetryOnNack = false,
    .enableLinMode = false,
    .enableCts = false,
    .ctsPolarity = CY_SCB_UART_ACTIVE_LOW,
    .rtsRxFifoLevel = 0u,
    .rtsPolarity = CY_SCB_UART_ACTIVE_LOW,
    .breakWidth = 11u,
    .breakLevel = false,
    .rxFifoTriggerLevel = 0u,
    .rxFifoIntEnableMask = 0u,
    .txFifoTriggerLevel = 0u,
    .txFifoIntEnableMask = 0u,
};

static const cy_stc_tcpwm_counter_config_t timer_config = {
    .period = 7499u,
    .clockPrescaler = CY_TCPWM_COUNTER_PRESCALER_DIVBY_32,
    .runMode = CY_TCPWM_COUNTER_CONTINUOUS,
    .countDirection = CY_TCPWM_COUNTER_COUNT_UP,
    .compareOrCapture = CY_TCPWM_COUNTER_MODE_CAPTURE,
    .compare0 = 0x4000u,
    .compare1 = 0x4000u,
    .enableCompareSwap = false,
    .interruptSources = CY_TCPWM_INT_ON_TC,
    .captureInputMode = CY_TCPWM_INPUT_LEVEL,
    .captureInput = CY_TCPWM_INPUT_0,
    .reloadInputMode = CY_TCPWM_INPUT_LEVEL,
    .reloadInput = CY_TCPWM_INPUT_0,
    .startInputMode = CY_TCPWM_INPUT_LEVEL,
    .startInput = CY_TCPWM_INPUT_0,
    .stopInputMode = CY_TCPWM_INPUT_LEVEL,
    .stopInput = CY_TCPWM_INPUT_0,
    .countInputMode = CY_TCPWM_INPUT_LEVEL,
    .countInput = CY_TCPWM_INPUT_1,
};

void Cy_SystemInit(void)
{
    Cy_SysLib_SetWaitStates(48u);
    Cy_SysClk_ImoEnable();
    (void)Cy_SysClk_ImoSetFrequency(CY_SYSCLK_IMO_48MHZ);
    (void)Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_IMO);
    SystemCoreClockUpdate();
}

static void uart_init(void)
{
    Cy_GPIO_Pin_FastInit(GPIO_PRT2, 4u, CY_GPIO_DM_HIGHZ, 0u,
                         P2_4_SCB3_UART_RX);
    Cy_GPIO_Pin_FastInit(GPIO_PRT2, 5u, CY_GPIO_DM_STRONG, 1u,
                         P2_5_SCB3_UART_TX);

    (void)Cy_SysClk_PeriphDisableDivider(CY_SYSCLK_DIV_16_BIT, 2u);
    (void)Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 2u, 5u);
    (void)Cy_SysClk_PeriphAssignDivider(PCLK_SCB3_CLOCK,
                                        CY_SYSCLK_DIV_16_BIT, 2u);
    (void)Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 2u);

    (void)Cy_SCB_UART_Init(SCB3, &uart_config, &uart_context);
    Cy_SCB_UART_Enable(SCB3);
}

static uint8_t uart_read_blocking(void)
{
    while (Cy_SCB_UART_GetNumInRxFifo(SCB3) == 0u) {
    }
    return (uint8_t)Cy_SCB_UART_Get(SCB3);
}

static void uart_put_factory_style(uint8_t value)
{
    /* The factory q/n/w path drops this byte if the TX FIFO is full. */
    (void)Cy_SCB_UART_Put(SCB3, value);
}

static void timer_init(void)
{
    (void)Cy_SysClk_PeriphDisableDivider(CY_SYSCLK_DIV_16_BIT, 1u);
    (void)Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 1u, 0u);
    (void)Cy_SysClk_PeriphAssignDivider(PCLK_TCPWM_CLOCKS0,
                                        CY_SYSCLK_DIV_16_BIT, 1u);
    (void)Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 1u);

    (void)Cy_TCPWM_Counter_Init(TCPWM, 0u, &timer_config);
    NVIC_ClearPendingIRQ(tcpwm_interrupts_0_IRQn);
    NVIC_SetPriority(tcpwm_interrupts_0_IRQn, 3u);
    NVIC_EnableIRQ(tcpwm_interrupts_0_IRQn);
    Cy_TCPWM_Counter_Enable(TCPWM, 0u);
    Cy_TCPWM_TriggerStart(TCPWM, 1u);
}

static void inference_timer_init(void)
{
    SysTick->LOAD = SysTick_LOAD_RELOAD_Msk;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

static void capsense_irq_init(void)
{
    NVIC_ClearPendingIRQ(msc_0_interrupt_IRQn);
    NVIC_SetPriority(msc_0_interrupt_IRQn, 2u);
    NVIC_EnableIRQ(msc_0_interrupt_IRQn);

    NVIC_ClearPendingIRQ(msc_1_interrupt_IRQn);
    NVIC_SetPriority(msc_1_interrupt_IRQn, 2u);
    NVIC_EnableIRQ(msc_1_interrupt_IRQn);
}

static bool capsense_config_init(void)
{
    (void)Cy_SysClk_PeriphDisableDivider(CY_SYSCLK_DIV_16_BIT, 0u);
    (void)Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 0u,
                                     CY_CAPSENSE_MODCLK_DIV - 1u);
    (void)Cy_SysClk_PeriphAssignDivider(PCLK_MSC0_CLOCK_MSC,
                                        CY_SYSCLK_DIV_16_BIT, 0u);
    (void)Cy_SysClk_PeriphAssignDivider(PCLK_MSC1_CLOCK_MSC,
                                        CY_SYSCLK_DIV_16_BIT, 0u);
    (void)Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 0u);

    if (CY_CAPSENSE_STATUS_SUCCESS != Cy_CapSense_Init(&cy_capsense_context)) {
        return false;
    }

    return CY_CAPSENSE_STATUS_SUCCESS ==
           Cy_CapSense_SetInactiveElectrodeState(
               CY_CAPSENSE_SNS_CONNECTION_SHIELD,
               CY_CAPSENSE_CSD_GROUP,
               &cy_capsense_context);
}

static bool capsense_enable(void)
{
    capsense_irq_init();
    __enable_irq();

    if (CY_CAPSENSE_STATUS_SUCCESS != Cy_CapSense_Enable(&cy_capsense_context)) {
        return false;
    }

    coinft_capsense_prepare_frames();
    capsense_ready = true;
    return true;
}

static bool name_has_terminator(void)
{
    for (uint32_t i = 0u; i < DEVICE_NAME_BYTES; ++i) {
        if (device_name[i] == 0x0Du) {
            return true;
        }
    }
    return false;
}

static void load_or_create_device_name(void)
{
    (void)Cy_Em_EEPROM_Read(0x40u, device_name, DEVICE_NAME_BYTES,
                            &eeprom_context);
    if (name_has_terminator()) {
        return;
    }

    for (uint32_t i = 0u; i < DEVICE_NAME_BYTES; ++i) {
        device_name[i] = default_device_name[i];
    }
    (void)Cy_Em_EEPROM_Write(0x40u, device_name, DEVICE_NAME_BYTES,
                             &eeprom_context);
}

static bool storage_init(void)
{
    if (CY_EM_EEPROM_SUCCESS !=
        Cy_Em_EEPROM_Init(&eeprom_config, &eeprom_context)) {
        return false;
    }

    (void)Cy_Em_EEPROM_Read(0u, scan_parameters, SCAN_PARAMETER_BYTES,
                            &eeprom_context);

    /* Factory startup stops at the first zero in table A. */
    for (uint32_t channel = 0u; channel < OUTPUT_CHANNELS; ++channel) {
        if (SCAN_PARAMETER_A[channel] == 0u) {
            break;
        }
        (void)coinft_capsense_apply_parameters(
            channel, SCAN_PARAMETER_A[channel], SCAN_PARAMETER_B[channel]);
    }

    load_or_create_device_name();
    return true;
}

static void send_raw_frame(void)
{
    uint8_t frame[UART_FRAME_BYTES];
    frame[0] = 0x02u;

    for (uint32_t channel = 0u; channel < OUTPUT_CHANNELS; ++channel) {
        const uint16_t raw = coinft_capsense_raw(channel);
        frame[1u + channel * 2u] = (uint8_t)raw;
        frame[2u + channel * 2u] = (uint8_t)(raw >> 8u);
    }

    frame[UART_FRAME_BYTES - 1u] = 0x03u;
    Cy_SCB_WriteArrayBlocking(SCB3, frame, UART_FRAME_BYTES);
}

void msc_0_interrupt_IRQHandler(void)
{
    Cy_CapSense_InterruptHandler(NULL, &cy_capsense_context);
}

void msc_1_interrupt_IRQHandler(void)
{
    Cy_CapSense_InterruptHandler(NULL, &cy_capsense_context);
}

void tcpwm_interrupts_0_IRQHandler(void)
{
    Cy_TCPWM_ClearInterrupt(TCPWM, 0u, CY_TCPWM_INT_ON_TC);

    if (streaming) {
        send_raw_frame();
        if (++indicator_count > 2u) {
            indicator_count = 0u;
            Cy_GPIO_Inv(GPIO_PRT2, 0u);
        }
    }
}

static void command_n(void)
{
    load_or_create_device_name();
    for (uint32_t i = 0u; i < DEVICE_NAME_BYTES; ++i) {
        const uint8_t value = device_name[i];
        uart_put_factory_style(value);
        if (value == 0x0Du) {
            uart_put_factory_style(0x0Du);
            uart_put_factory_style(0x0Au);
            return;
        }
    }
}

static void command_w(void)
{
    uint32_t i = 0u;
    for (;;) {
        const uint8_t value = uart_read_blocking();
        device_name[i] = value;
        if (value == 0x0Du) {
            break;
        }
        if (++i == DEVICE_NAME_BYTES) {
            break;
        }
    }

    device_name[DEVICE_NAME_BYTES - 1u] = 0u;
    (void)Cy_Em_EEPROM_Write(0x40u, device_name, DEVICE_NAME_BYTES,
                             &eeprom_context);
    command_n();
}

static void command_t(void)
{
    uint8_t parameters[4];
    for (uint32_t i = 0u; i < 4u; ++i) {
        parameters[i] = uart_read_blocking();
    }

    const uint32_t channel = parameters[0];
    const uint8_t cdac_ref = parameters[2];
    const uint8_t cdac_comp = parameters[3];
    const bool saved_streaming = streaming;

    streaming = false;
    while (CY_CAPSENSE_NOT_BUSY != Cy_CapSense_IsBusy(&cy_capsense_context)) {
    }

    /* The factory image has no range check here. Keep legal requests exact,
     * but do not reproduce its SRAM/Flash out-of-bounds write. */
    if (channel < OUTPUT_CHANNELS) {
        SCAN_PARAMETER_A[channel] = cdac_ref;
        SCAN_PARAMETER_B[channel] = cdac_comp;
        (void)coinft_capsense_apply_parameters(channel, cdac_ref, cdac_comp);
    }

    (void)Cy_Em_EEPROM_Write(0u, scan_parameters, SCAN_PARAMETER_BYTES,
                             &eeprom_context);
    (void)Cy_CapSense_Enable(&cy_capsense_context);
    coinft_capsense_prepare_frames();
    tare_valid = false;
    streaming = saved_streaming;
}

static void command_p(void);
static void command_z(void);
static void send_mlp_diagnostic(void);

static void handle_idle_command(uint8_t command)
{
    switch (command) {
    case (uint8_t)'q':
        uart_put_factory_style(0x1Au);
        break;
    case (uint8_t)'s':
        if (capsense_ready) {
            scan_phase = 0u;
            streaming = true;
        }
        break;
    case (uint8_t)'w':
        command_w();
        break;
    case (uint8_t)'n':
        command_n();
        break;
    case (uint8_t)'p':
        if (capsense_ready) {
            command_p();
        }
        break;
    case (uint8_t)'z':
        if (capsense_ready) {
            command_z();
        }
        break;
    case (uint8_t)'r':
        if (tare_valid && inference_sequence != 0u) {
            send_mlp_diagnostic();
        }
        break;
    default:
        break;
    }
}

static void scan_and_wait(uint8_t phase)
{
    const uint32_t reference_state = (phase == 0u) ? 2u : 0u;
    const uint32_t first_slot = (phase == 0u) ? 0u : 4u;
    const uint32_t slot_count = (phase == 0u) ? 4u : 2u;

    (void)Cy_CapSense_SlotPinState_V3(COINFT_REFERENCE_SLOT_ID,
                                      &coinft_reference_electrode,
                                      reference_state,
                                      &cy_capsense_context);

    if (phase != 0u) {
        while (CY_CAPSENSE_NOT_BUSY !=
               Cy_CapSense_IsBusy(&cy_capsense_context)) {
        }
    }

    (void)Cy_CapSense_ScanSlots(first_slot, slot_count,
                                &cy_capsense_context);
    while (CY_CAPSENSE_NOT_BUSY !=
           Cy_CapSense_IsBusy(&cy_capsense_context)) {
    }
}

static void scan_complete_pair(void)
{
    scan_and_wait(0u);
    scan_and_wait(1u);
    scan_phase = 0u;
}

static void snapshot_raw(void)
{
    for (uint32_t channel = 0u; channel < COINFT_MLP_INPUTS; ++channel) {
        latest_raw[channel] = coinft_capsense_raw(channel);
    }
}

static void capture_tare(void)
{
    snapshot_raw();
    for (uint32_t channel = 0u; channel < COINFT_MLP_INPUTS; ++channel) {
        tare_offset[channel] = latest_raw[channel];
    }
    tare_valid = true;
}

static void run_inference(void)
{
    snapshot_raw();
    if (!tare_valid) {
        for (uint32_t channel = 0u; channel < COINFT_MLP_INPUTS; ++channel) {
            tare_offset[channel] = latest_raw[channel];
        }
        tare_valid = true;
    }

    for (uint32_t channel = 0u; channel < COINFT_MLP_INPUTS; ++channel) {
        latest_zeroed[channel] =
            (int32_t)latest_raw[channel] - tare_offset[channel];
    }

    const uint32_t start = SysTick->VAL;
    coinft_mlp_infer(latest_zeroed, &latest_mlp);
    const uint32_t end = SysTick->VAL;
    latest_mlp_cycles = (start - end) & SysTick_LOAD_RELOAD_Msk;
    ++inference_sequence;
}

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8u);
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8u);
    destination[2] = (uint8_t)(value >> 16u);
    destination[3] = (uint8_t)(value >> 24u);
}

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t size)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t index = 0u; index < size; ++index) {
        crc ^= (uint16_t)data[index] << 8u;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                  (uint16_t)((crc << 1u) ^ 0x1021u) :
                  (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static void send_mlp_diagnostic(void)
{
    uint8_t frame[MLP_DIAGNOSTIC_BYTES];
    uint32_t offset = 0u;
    frame[offset++] = 0xA5u;
    frame[offset++] = 0x5Au;
    frame[offset++] = MLP_DIAGNOSTIC_VERSION;
    frame[offset++] = MLP_DIAGNOSTIC_BYTES;
    put_u32_le(&frame[offset], inference_sequence);
    offset += 4u;

    for (uint32_t channel = 0u; channel < COINFT_MLP_INPUTS; ++channel) {
        put_u16_le(&frame[offset], latest_raw[channel]);
        offset += 2u;
    }
    for (uint32_t channel = 0u; channel < COINFT_MLP_INPUTS; ++channel) {
        put_u32_le(&frame[offset], (uint32_t)latest_zeroed[channel]);
        offset += 4u;
    }
    for (uint32_t axis = 0u; axis < COINFT_MLP_OUTPUTS; ++axis) {
        put_u32_le(&frame[offset], (uint32_t)latest_mlp.output_micro[axis]);
        offset += 4u;
    }
    put_u32_le(&frame[offset], latest_mlp_cycles);
    offset += 4u;
    put_u16_le(&frame[offset], latest_mlp.saturation_count);
    offset += 2u;
    put_u16_le(&frame[offset], tare_valid ? 1u : 0u);
    offset += 2u;
    put_u16_le(&frame[offset], crc16_ccitt(frame, offset));
    offset += 2u;

    if (offset == MLP_DIAGNOSTIC_BYTES) {
        Cy_SCB_WriteArrayBlocking(SCB3, frame, MLP_DIAGNOSTIC_BYTES);
    }
}

static void command_p(void)
{
    scan_complete_pair();
    run_inference();
    send_mlp_diagnostic();
}

static void command_z(void)
{
    scan_complete_pair();
    capture_tare();
    run_inference();
    uart_put_factory_style((uint8_t)'Z');
}

int main(void)
{
    Cy_GPIO_Pin_FastInit(GPIO_PRT2, 0u, CY_GPIO_DM_STRONG_IN_OFF, 1u,
                         P2_0_GPIO);
    uart_init();

    if (!capsense_config_init() || !storage_init() || !capsense_enable()) {
        for (;;) {
        }
    }

    timer_init();
    inference_timer_init();
    __enable_irq();

    for (;;) {
        uint32_t received = UINT32_MAX;
        if (Cy_SCB_UART_GetNumInRxFifo(SCB3) != 0u) {
            received = Cy_SCB_UART_Get(SCB3);
        }

        if ((uint8_t)received == (uint8_t)'t') {
            command_t();
        }

        if (!streaming) {
            handle_idle_command((uint8_t)received);
            continue;
        }

        scan_and_wait(scan_phase);
        scan_phase ^= 1u;

        if (scan_phase == 0u) {
            run_inference();
        }

        if ((uint8_t)received == (uint8_t)'i') {
            streaming = false;
            scan_phase = 0u;
            indicator_count = 0u;
            Cy_GPIO_Set(GPIO_PRT2, 0u);
        }
    }
}
