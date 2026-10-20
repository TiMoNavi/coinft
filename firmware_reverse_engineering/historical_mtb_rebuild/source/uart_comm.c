/**
 * UART 通信
 *
 * 使用 PDL 的 SCB UART 驱动
 * 从逆向工程推断：SCB0 用于 UART，115200 baud，8N1
 */

#include "uart_comm.h"
#include "cy_pdl.h"
#include "cy_device.h"

/* UART 硬件配置 */
#define UART_HW         SCB0
#define UART_IRQ        scb_0_interrupt_IRQn

/* UART 上下文 */
static cy_stc_scb_uart_context_t g_uart_context;

void uart_comm_init(void)
{
    /* 1. 配置 GPIO 引脚为 UART 功能
     * 从逆向工程和数据手册：
     * SCB0 UART 可以路由到 P4[0]/P4[1]
     * HSIOM 值 6 通常对应 SCB 外设功能
     */

    /* 配置 P4[1] 为 SCB0 TX (输出，强驱动) */
    Cy_GPIO_Pin_FastInit(GPIO_PRT4, 1, CY_GPIO_DM_STRONG_IN_OFF, 1, 6);

    /* 配置 P4[0] 为 SCB0 RX (输入，高阻) */
    Cy_GPIO_Pin_FastInit(GPIO_PRT4, 0, CY_GPIO_DM_HIGHZ, 1, 6);

    /* 2. UART 配置：115200 baud, 8N1 */
    cy_stc_scb_uart_config_t uart_config = {
        .uartMode = CY_SCB_UART_STANDARD,
        .oversample = 12,  /* 48 MHz / 12 / 40 ≈ 100 kHz → 接近 115200 */
        .dataWidth = 8,
        .enableMsbFirst = false,
        .stopBits = CY_SCB_UART_STOP_BITS_1,
        .parity = CY_SCB_UART_PARITY_NONE,
        .enableInputFilter = false,
        .dropOnParityError = false,
        .dropOnFrameError = false,
        .enableMutliProcessorMode = false,
        .receiverAddress = 0,
        .receiverAddressMask = 0,
        .acceptAddrInFifo = false,
        .irdaInvertRx = false,
        .irdaEnableLowPowerReceiver = false,
        .smartCardRetryOnNack = false,
        .enableLinMode = false,
        .enableCts = false,
        .ctsPolarity = CY_SCB_UART_ACTIVE_LOW,
        .rtsRxFifoLevel = 0,
        .rtsPolarity = CY_SCB_UART_ACTIVE_LOW,
        .breakWidth = 11,
        .breakLevel = false,
        .rxFifoTriggerLevel = 0,
        .rxFifoIntEnableMask = 0,
        .txFifoTriggerLevel = 63,
        .txFifoIntEnableMask = 0
    };

    /* 初始化 SCB0 为 UART 模式 */
    cy_en_scb_uart_status_t status = Cy_SCB_UART_Init(UART_HW, &uart_config, &g_uart_context);
    CY_ASSERT(status == CY_SCB_UART_SUCCESS);
    (void)status;

    /* 使能 UART */
    Cy_SCB_UART_Enable(UART_HW);
}

void uart_send_frame(const record_t records[NUM_CHANNELS])
{
    uint8_t frame[UART_FRAME_SIZE];

    /* SOF */
    frame[0] = FRAME_SOF;

    /* 12 路 raw（小端序）*/
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        uint16_t raw = records[ch].raw;
        frame[1 + ch*2]     = raw & 0xFF;        /* 低字节 */
        frame[1 + ch*2 + 1] = (raw >> 8) & 0xFF; /* 高字节 */
    }

    /* EOF */
    frame[25] = FRAME_EOF;

    /* 发送整帧（阻塞模式）*/
    Cy_SCB_UART_PutArray(UART_HW, frame, UART_FRAME_SIZE);
}

bool uart_rx_available(void)
{
    /* 检查 RX FIFO 是否有数据 */
    return (Cy_SCB_UART_GetNumInRxFifo(UART_HW) > 0);
}

uint8_t uart_read_byte(void)
{
    /* 从 RX FIFO 读取一个字节 */
    return (uint8_t)Cy_SCB_UART_Get(UART_HW);
}

void uart_send_byte(uint8_t data)
{
    /* 发送单字节 */
    Cy_SCB_UART_Put(UART_HW, data);
}

void uart_send_bytes(const uint8_t* data, uint8_t len)
{
    /* 发送多字节 */
    Cy_SCB_UART_PutArray(UART_HW, data, len);
}
