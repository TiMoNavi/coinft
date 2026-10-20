/**
 * CapSense 扫描引擎 - 第四代 MSC 直接寄存器控制
 *
 * 基于逆向工程 v18/v42 恢复的寄存器配置
 * 硬件：CY8C4149AZI-S593（使用第四代 MSC 兼容模式）
 */

#include "capsense_scan.h"
#include <stdint.h>
#include <stdbool.h>

/* MSC 寄存器定义（第四代，基址 0x40290000）*/
#define MSC0_BASE           0x40290000UL

#define MSC_CTL             (*(volatile uint32_t*)(MSC0_BASE + 0x00))
#define MSC_SNS_CTL         (*(volatile uint32_t*)(MSC0_BASE + 0x14))
#define MSC_SCAN_CTL1       (*(volatile uint32_t*)(MSC0_BASE + 0x50))
#define MSC_SW_SEL_CSW      (*(volatile uint32_t*)(MSC0_BASE + 0x30))
#define MSC_SW_SEL_GPIO     (*(volatile uint32_t*)(MSC0_BASE + 0x34))
#define MSC_RESULT1         (*(volatile uint32_t*)(MSC0_BASE + 0x88))
#define MSC_INTR            (*(volatile uint32_t*)(MSC0_BASE + 0x170))
#define MSC_INTR_MASK       (*(volatile uint32_t*)(MSC0_BASE + 0x178))

/* 中断标志位 */
#define MSC_INTR_INIT           (1UL << 0)
#define MSC_INTR_CONFIG_REQ     (1UL << 1)
#define MSC_INTR_SCAN_COMPLETE  (1UL << 2)

/* 全局状态 */
static volatile bool g_scan_complete = false;
static uint8_t g_current_slot = 0;
static volatile uint16_t g_raw_results[NUM_CHANNELS];  /* 存储 12 路 raw 值 */

/* 14 个 slot 配置（从 v18/v42 恢复，简化版）*/
typedef struct {
    uint32_t sns_ctl;
    uint32_t scan_ctl1;
    uint32_t sw_sel_csw;
    uint32_t sw_sel_gpio;
} slot_config_t;

/* 配置表（示例，需要从 v18/v42 完整复制）*/
static const slot_config_t g_slot_configs[14] = {
    /* slot 0 */
    {
        .sns_ctl = 0x00C800C8,      /* sense_width=8449, lfsr_mode=2 */
        .scan_ctl1 = 0x00000400,
        .sw_sel_csw = 0x00000000,
        .sw_sel_gpio = 0x0000007E,
    },
    /* slot 1 - 需要从逆向结果补全 */
    {
        .sns_ctl = 0x00C800C8,
        .scan_ctl1 = 0x00000400,
        .sw_sel_csw = 0x00000000,
        .sw_sel_gpio = 0x0000007E,
    },
    /* ... 其余 12 个 slot */
};

/* slot 到通道的映射（从逆向结果，slot 6 和 13 是特殊配置）*/
static const int8_t g_slot_to_channel[14] = {
    0, 1, 2, 3, 4, 5,  /* slot 0-5 */
    -1,                 /* slot 6: 特殊配置 */
    6, 7, 8, 9, 10, 11, /* slot 7-12 */
    -1                  /* slot 13: LAST */
};

void capsense_scan_init(void)
{
    /* 1. 配置 MSC 控制寄存器 */
    MSC_CTL = 0x81100011;  /* 从逆向结果 */

    /* 2. 使能中断 */
    MSC_INTR_MASK = MSC_INTR_INIT | MSC_INTR_CONFIG_REQ | MSC_INTR_SCAN_COMPLETE;

    /* 3. 清除标志 */
    g_scan_complete = false;
    g_current_slot = 0;

    /* TODO: 配置 GPIO 为 MSC 模式（需要 GPIO 寄存器操作）*/
}

void capsense_start(void)
{
    g_current_slot = 0;
    g_scan_complete = false;

    /* 配置 slot 0 */
    MSC_SNS_CTL = g_slot_configs[0].sns_ctl;
    MSC_SCAN_CTL1 = g_slot_configs[0].scan_ctl1;
    MSC_SW_SEL_CSW = g_slot_configs[0].sw_sel_csw;
    MSC_SW_SEL_GPIO = g_slot_configs[0].sw_sel_gpio;

    /* 触发 INIT 中断启动扫描 */
    MSC_INTR = MSC_INTR_INIT;
}

void capsense_stop(void)
{
    /* 清除中断标志，停止扫描 */
    MSC_INTR = 0;
    g_scan_complete = false;
}

bool capsense_is_scan_complete(void)
{
    /* 检查中断标志 */
    if (MSC_INTR & MSC_INTR_SCAN_COMPLETE) {
        g_scan_complete = true;
        return true;
    }
    return g_scan_complete;
}

void capsense_read_results(uint16_t raw[NUM_CHANNELS])
{
    /* 从 MSC_RESULT1 读取 12 路结果
     * 注意：需要在每个 slot 扫描完成后读取
     *
     * 当前简化实现：假设所有结果已存储在某处
     * 实际需要在 CONFIG_REQ ISR 中逐个读取
     */

    /* 占位实现：返回固定值（需要替换为实际 ISR 读取的结果）*/
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        raw[ch] = 2048;  /* 默认值 */
    }
}

void capsense_clear_scan_flag(void)
{
    /* 清除 SCAN_COMPLETE 标志并启动下一次扫描 */
    MSC_INTR = MSC_INTR_SCAN_COMPLETE;
    g_scan_complete = false;

    /* 重新启动扫描 */
    capsense_start();
}

/* TODO: 中断服务程序（需要在启动代码中注册）
 *
 * void MSC_IRQHandler(void) {
 *     uint32_t intr = MSC_INTR;
 *
 *     if (intr & MSC_INTR_CONFIG_REQ) {
 *         // 配置下一个 slot
 *         g_current_slot++;
 *         if (g_current_slot < 14) {
 *             MSC_SNS_CTL = g_slot_configs[g_current_slot].sns_ctl;
 *             MSC_SCAN_CTL1 = g_slot_configs[g_current_slot].scan_ctl1;
 *             MSC_SW_SEL_CSW = g_slot_configs[g_current_slot].sw_sel_csw;
 *             MSC_SW_SEL_GPIO = g_slot_configs[g_current_slot].sw_sel_gpio;
 *         }
 *
 *         // 读取上一个 slot 的结果
 *         if (g_current_slot > 0 && g_current_slot <= 13) {
 *             int8_t ch = g_slot_to_channel[g_current_slot - 1];
 *             if (ch >= 0) {
 *                 uint16_t result = MSC_RESULT1 & 0xFFFF;
 *                 // 存储到全局数组（需要定义）
 *             }
 *         }
 *
 *         MSC_INTR = MSC_INTR_CONFIG_REQ;
 *     }
 *
 *     if (intr & MSC_INTR_SCAN_COMPLETE) {
 *         g_scan_complete = true;
 *         // 不在这里清除标志，由主循环清除
 *     }
 * }
 */

/* ===== 实际实现 ===== */

uint16_t capsense_read_raw(uint8_t channel)
{
    if (channel >= NUM_CHANNELS) {
        return 0;
    }
    return g_raw_results[channel];
}

/* MSC 中断服务程序 */
void msc_0_interrupt_IRQHandler(void)
{
    uint32_t intr = MSC_INTR;

    /* CONFIG_REQ 中断：需要配置下一个 slot */
    if (intr & MSC_INTR_CONFIG_REQ) {
        /* 配置下一个 slot */
        g_current_slot++;

        if (g_current_slot < 14) {
            MSC_SNS_CTL = g_slot_configs[g_current_slot].sns_ctl;
            MSC_SCAN_CTL1 = g_slot_configs[g_current_slot].scan_ctl1;
            MSC_SW_SEL_CSW = g_slot_configs[g_current_slot].sw_sel_csw;
            MSC_SW_SEL_GPIO = g_slot_configs[g_current_slot].sw_sel_gpio;
        }

        /* 读取上一个 slot 的结果并存储 */
        if (g_current_slot > 0 && g_current_slot <= 14) {
            int8_t ch = g_slot_to_channel[g_current_slot - 1];
            if (ch >= 0 && ch < NUM_CHANNELS) {
                g_raw_results[ch] = (uint16_t)(MSC_RESULT1 & 0xFFFF);
            }
        }

        /* 清除 CONFIG_REQ 中断标志 */
        MSC_INTR = MSC_INTR_CONFIG_REQ;
    }

    /* SCAN_COMPLETE 中断：一轮 14 个 slot 扫描完成 */
    if (intr & MSC_INTR_SCAN_COMPLETE) {
        g_scan_complete = true;
        /* 不在这里清除 SCAN_COMPLETE 标志，由主循环清除 */
    }
}

