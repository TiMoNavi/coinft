/**
 * CoinFT 固件重构 - 主程序
 *
 * 状态机：idle → streaming → blocked
 */

#include "cy_pdl.h"
#include "cy_device.h"
#include "cybsp.h"
#include "coinft_types.h"
#include "record.h"
#include "uart_comm.h"
#include "capsense_scan.h"
#include "commands.h"
#include "eeprom_mgr.h"

/* 调试开关：1 = 最简测试，0 = 完整功能 */
#define DEBUG_MINIMAL_TEST 1

#if DEBUG_MINIMAL_TEST

/* 最简测试：使用 LED 闪烁验证代码执行 + UART 测试 */

/* SCB0 寄存器 */
#define SCB0_BASE       0x40240000
#define SCB_CTRL        (*(volatile uint32_t*)(SCB0_BASE + 0x00))
#define SCB_UART_CTRL   (*(volatile uint32_t*)(SCB0_BASE + 0x40))
#define SCB_UART_TX_CTRL (*(volatile uint32_t*)(SCB0_BASE + 0x44))
#define SCB_UART_RX_CTRL (*(volatile uint32_t*)(SCB0_BASE + 0x48))
#define SCB_TX_FIFO_CTRL (*(volatile uint32_t*)(SCB0_BASE + 0x80))
#define SCB_TX_FIFO_WR   (*(volatile uint32_t*)(SCB0_BASE + 0x88))
#define SCB_TX_FIFO_STATUS (*(volatile uint32_t*)(SCB0_BASE + 0x84))
#define SCB_UART_FLOW_CTRL (*(volatile uint32_t*)(SCB0_BASE + 0x4C))

/* GPIO P4 寄存器 */
#define GPIO_PRT4_BASE  0x40040400
#define GPIO_PRT4_DR    (*(volatile uint32_t*)(GPIO_PRT4_BASE + 0x00))
#define GPIO_PRT4_PC    (*(volatile uint32_t*)(GPIO_PRT4_BASE + 0x08))
#define GPIO_PRT4_PC2   (*(volatile uint32_t*)(GPIO_PRT4_BASE + 0x0C))

/* HSIOM P4 寄存器 */
#define HSIOM_PRT4_BASE 0x40030400
#define HSIOM_PRT4_PORT_SEL0 (*(volatile uint32_t*)(HSIOM_PRT4_BASE + 0x00))

/* 时钟寄存器 - 需要给 SCB0 配置时钟 */
#define PERI_BASE       0x40010000
#define PERI_PCLK_CTL0  (*(volatile uint32_t*)(PERI_BASE + 0x4000))  /* SCB0 时钟 */
#define PERI_DIV_16_CTL0 (*(volatile uint32_t*)(PERI_BASE + 0x0200))

static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms * 8000; i++) {
        __NOP();
    }
}

static void uart_send_byte_direct(uint8_t data)
{
    /* 等待 TX FIFO 有空间 */
    while ((SCB_TX_FIFO_STATUS & 0x0F) >= 8);

    /* 写入数据 */
    SCB_TX_FIFO_WR = data;
}

int main(void)
{
    /* 1. 基础初始化 */
    cybsp_init();
    __enable_irq();

    delay_ms(100);

    /* 2. 配置 P4[1] 为 TX */
    /* 设置为强驱动模式 (drive mode = 6) */
    uint32_t pc_val = GPIO_PRT4_PC;
    pc_val &= ~(0x0E << 3);  /* 清除 P4[1] 的驱动模式位 */
    pc_val |= (0x06 << 3);   /* 设置为强驱动 */
    GPIO_PRT4_PC = pc_val;

    /* 设置 HSIOM 为 SCB0_UART_TX (通常是 6) */
    uint32_t hsiom_val = HSIOM_PRT4_PORT_SEL0;
    hsiom_val &= ~(0x1F << 8);  /* 清除 P4[1] 的 HSIOM */
    hsiom_val |= (6 << 8);      /* HSIOM = 6 */
    HSIOM_PRT4_PORT_SEL0 = hsiom_val;

    /* 3. 配置 SCB0 时钟：从 HFCLK 分频，不分频使用 48 MHz */
    PERI_DIV_16_CTL0 = 0x00000000;  /* 分频器=1 (div-1=0) */
    PERI_PCLK_CTL0 = 0x00000040;    /* 使能，选择 16-bit divider #0 */

    delay_ms(10);

    /* 4. 配置 SCB0 为 UART 模式 */
    /* 先禁用 */
    SCB_CTRL = 0x00000000;

    /* 配置 UART 参数 */
    /* 115200 baud: 48MHz / 12 / 34.7 ≈ 115200 */
    SCB_UART_CTRL = 0x00000000;      /* 标准模式，8位数据 */
    SCB_UART_TX_CTRL = 0x0000010B;   /* oversample-1=11 (12), data_width=8 */
    SCB_UART_RX_CTRL = 0x00000107;   /* data_width=8 */
    SCB_UART_FLOW_CTRL = 0x00000000; /* 无流控 */

    /* TX FIFO */
    SCB_TX_FIFO_CTRL = 0x00000000;   /* trigger level = 0 */

    /* 使能 SCB，模式=UART (2) */
    SCB_CTRL = 0x82000000;  /* bit 31=ENABLED, bits 25:24=10 (UART mode) */

    delay_ms(100);

    /* 5. 循环发送测试消息 */
    uint8_t counter = 0;
    while (1) {
        /* 发送: 0x02 <counter> "OK" 0x03 \r\n */
        uart_send_byte_direct(0x02);  // SOF (正确的标记!)
        uart_send_byte_direct(counter);
        uart_send_byte_direct('O');
        uart_send_byte_direct('K');
        uart_send_byte_direct(0x03);  // EOF (正确的标记!)
        uart_send_byte_direct('\r');
        uart_send_byte_direct('\n');

        counter++;
        delay_ms(1000);
    }

    return 0;
}

#else

/* 全局状态 */
static volatile system_state_t g_state = STATE_IDLE;

/* 系统初始化 */
static void system_init(void)
{
    /* 1. BSP 初始化（时钟、HAL）*/
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    (void)result;  /* 避免 Release 模式未使用警告 */

    /* 2. 配置系统时钟
     * PSoC 4 默认使用 IMO (Internal Main Oscillator)
     * PSoC 4100S Max 的 IMO 默认为 48 MHz，无需额外配置
     */

    /* 3. 使能全局中断 */
    __enable_irq();

    /* 4. 初始化各功能模块 */
    record_init();
    uart_comm_init();
    capsense_scan_init();
    cmd_parser_init();
    eeprom_mgr_init();
}

/* 主循环 */
static void main_loop(void)
{
    while (1) {
        switch (g_state) {
            case STATE_IDLE:
                /* 等待 's' 命令启动 */
                if (uart_rx_available()) {
                    uint8_t cmd = uart_read_byte();
                    if (cmd == CMD_STREAMING) {
                        capsense_start();
                        g_state = STATE_STREAMING;
                    }
                }
                break;
                
            case STATE_STREAMING:
                /* 检查扫描是否完成 */
                if (capsense_is_scan_complete()) {
                    uint16_t raw[NUM_CHANNELS];
                    capsense_read_results(raw);
                    
                    /* 更新 record */
                    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                        record_update_raw(ch, raw[ch]);
                    }
                    record_update_diff_counters();
                    
                    /* 发送 UART 帧 */
                    record_t records_copy[NUM_CHANNELS];
                    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                        records_copy[ch] = *record_get(ch);
                    }
                    uart_send_frame(records_copy);
                    
                    capsense_clear_scan_flag();
                }
                
                /* 检查命令 */
                if (uart_rx_available()) {
                    uint8_t cmd = uart_read_byte();
                    
                    if (cmd == CMD_IDLE) {
                        capsense_stop();
                        g_state = STATE_IDLE;
                    }
                    else if (cmd == CMD_QUERY) {
                        cmd_handle_query();
                    }
                    else if (cmd == CMD_TARE || cmd == CMD_WRITE_ID) {
                        /* 进入阻塞状态，等待剩余字节 */
                        cmd_parser_feed_byte(cmd);
                        g_state = STATE_BLOCKED;
                    }
                    else if (cmd == CMD_READ_ID) {
                        cmd_handle_read_id();
                    }
                }
                break;
                
            case STATE_BLOCKED:
                /* 收集命令字节 */
                if (uart_rx_available()) {
                    uint8_t byte = uart_read_byte();
                    cmd_parser_feed_byte(byte);
                    
                    if (cmd_parser_is_complete()) {
                        cmd_parser_execute(&g_state);
                    }
                }
                break;
        }
    }
}

int main(void)
{
    system_init();
    main_loop();

    /* Never reached */
    return 0;
}

#endif  /* DEBUG_MINIMAL_TEST */
