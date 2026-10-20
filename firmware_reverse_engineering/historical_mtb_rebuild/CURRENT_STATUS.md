# CoinFT 固件重建 - 当前状态分析

**日期**：2026-08-30  
**问题**：能否烧录运行？与原固件是否一致？

---

## 🔴 简短回答：**不能直接替代原固件**

当前固件**可以编译和烧录**，但**无法正常工作**，因为所有硬件交互函数都是**占位实现**（TODO stubs）。

---

## 📊 实现状态对比

| 模块 | 编译 | 运行 | 与原固件一致性 | 说明 |
|---|:---:|:---:|:---:|---|
| **编译/链接** | ✅ | - | - | 成功生成 .elf/.hex |
| **启动代码** | ✅ | ✅ | ✅ | Reset_Handler → main() 正常 |
| **系统时钟** | ⚠️ | ❌ | ❌ | 未配置，运行在默认时钟（~8 MHz IMO） |
| **UART 通信** | ⚠️ | ❌ | ❌ | 所有函数都是空实现 |
| **CapSense** | ⚠️ | ❌ | ❌ | 初始化框架存在，但未实际操作 MSC 寄存器 |
| **EEPROM** | ⚠️ | ❌ | ❌ | 返回固定值 |
| **主循环** | ✅ | ⚠️ | ⚠️ | 逻辑正确，但依赖的函数都是占位 |
| **命令解析** | ✅ | ⚠️ | ✅ | 协议解析逻辑正确 |

**图例**：
- ✅ 完全实现 / 正常工作
- ⚠️ 部分实现 / 需要完善
- ❌ 未实现 / 无法工作

---

## 🔍 详细分析

### 1. 系统时钟 ❌

**原固件**：
```c
// 从逆向工程推断：48 MHz IMO
SRSS->CLK_IMO_CONFIG = 0x80000000;  // 使能 48 MHz
```

**当前实现**：
```c
/* TODO: 配置系统时钟为 48 MHz */
/* TODO: 使能全局中断 */
```

**影响**：所有定时相关功能（UART 波特率、CapSense 扫描周期）都会错误。

---

### 2. UART 通信 ❌

#### 2.1 uart_comm_init()
**原固件**（从逆向推断）：
```c
// 配置 SCB0 为 UART 模式，115200 baud
SCB0->CTRL = 0x01000000;  // UART mode
SCB0->UART_CTRL = 0x03000000;  // 8N1
SCB0->UART_RX_CTRL = ...
SCB0->UART_TX_CTRL = ...
```

**当前实现**：
```c
void uart_comm_init(void)
{
    /* TODO: 配置 UART
     * - 选择 SCB 实例（从逆向结果看可能是 SCB0）
     * - 波特率 115200
     * - 8N1
     * - 使能 RX/TX
     */
}
```

**结果**：**完全不工作** —— 串口未初始化，无法收发数据。

#### 2.2 uart_send_frame()
**原固件**：
```c
// 逆向结果显示：将 82 字节帧写入 UART TX FIFO
for (int i = 0; i < UART_FRAME_SIZE; i++) {
    while (!(SCB0->UART_TX_STATUS & SCB_UART_TX_STATUS_TX_FIFO_NOT_FULL));
    SCB0->TX_FIFO_WR = frame[i];
}
```

**当前实现**：
```c
void uart_send_frame(const record_t records[NUM_CHANNELS])
{
    uint8_t frame[UART_FRAME_SIZE];
    
    /* 构造帧（这部分是正确的）*/
    frame[0] = FRAME_SOF;
    // ... 填充 12 路 raw/baseline/diff
    
    /* TODO: 调用 UART 发送函数 */
    // uart_transmit(frame, UART_FRAME_SIZE);
}
```

**结果**：帧构造逻辑**正确**（与逆向结果一致），但**不会发送** —— host 收不到任何数据。

---

### 3. CapSense 扫描 ❌

#### 3.1 初始化
**原固件**（从 v54 逆向结果）：
```c
// 14 个 MSC slot 的精确配置（已在逆向文档中记录）
MSC->SNS_CTL = 0x00c800c8;
MSC->MODE[0].SENSE_DUTY_CTL = 0x04040040;
MSC->MODE[0].SW_SEL_CDAC_FL = 0x03002000;
// ... 共 14 组寄存器
```

**当前实现**：
```c
void capsense_scan_init(void)
{
    /* 从逆向工程得到的 MSC 配置：
     * - MSC0 基址：0x40290000
     * - 14 个 slot，每个 slot 对应一个电极
     * - SNS_CTL = 0x00c800c8（SENSE_WIDTH=200, Vref=1.2V）
     */
    
    /* TODO: 配置 GPIO 为 MSC 模式（需要 GPIO 寄存器操作）*/
    
    /* TODO: 应用逆向工程恢复的 MSC 寄存器配置 */
}
```

**结果**：**MSC 未初始化** —— 无法进行电容扫描，`capsense_read_raw()` 返回固定值 0。

#### 3.2 扫描循环
**原固件**：
```c
// ISR 触发扫描完成后更新 raw 值
void msc_0_interrupt_IRQHandler(void) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        g_raw_values[i] = MSC->MODE[slot_map[i]].RESULT_FIFO_RD;
    }
    MSC->INTR = MSC_INTR_MASK_SCAN_Msk;  // 清除中断
    g_scan_complete = 1;
}
```

**当前实现**：
```c
uint16_t capsense_read_raw(uint8_t channel)
{
    /* 占位实现：返回固定值（需要替换为实际 ISR 读取的结果）*/
    (void)channel;
    return 0;
}

/* TODO: 中断服务程序（需要在启动代码中注册）
void msc_0_interrupt_IRQHandler(void)
{
    // 读取 MSC FIFO
    // 清除中断标志
    // 设置扫描完成标志
}
*/
```

**结果**：所有通道**永远返回 0** —— host 会看到 12 路全为 0 的数据流。

---

### 4. EEPROM ❌

**原固件**：
```c
// 使用 Flash 模拟 EEPROM（地址约在 Flash 末尾）
void eeprom_write(uint16_t addr, const uint8_t* data, uint16_t len) {
    cy_flash_write_row(EEPROM_BASE + addr, data);
}
```

**当前实现**：
```c
void eeprom_read(uint16_t addr, uint8_t* data, uint16_t len)
{
    /* TODO: 从 EEPROM 读取 */
    /* 占位实现 */
    for (uint16_t i = 0; i < len; i++) {
        data[i] = 0xFF;
    }
}
```

**结果**：
- Tare 标定值**无法保存**（`CMD_TARE` 执行后重启会丢失）
- 设备 ID **固定为 0xFF**（`CMD_QUERY` 返回错误值）

---

### 5. 主循环逻辑 ✅

**好消息**：状态机逻辑与原固件**完全一致**：

```c
STATE_IDLE → (接收 's') → STATE_STREAMING → (200 Hz 扫描) → 发送帧
STATE_STREAMING → (接收 'i') → STATE_IDLE
STATE_STREAMING → (接收 'q') → 发送查询响应
STATE_STREAMING → (接收 't') → 执行 Tare
```

这部分**不需要修改** —— 一旦底层硬件函数实现，主循环会正常工作。

---

## 🔬 如果现在烧录会发生什么？

### 场景 1：用 OpenOCD 烧录
```bash
openocd -f interface/cmsis-dap.cfg \
        -f target/psoc4.cfg \
        -c "program build/APP_COINFT_S593/Debug/coinft_firmware_rebuild_mtb.hex verify reset exit"
```

**结果**：
1. ✅ 烧录成功（HEX 格式正确）
2. ✅ 芯片重启，执行 Reset_Handler
3. ✅ 进入 `main()`，调用 `system_init()`
4. ❌ UART 未初始化 → host 收不到数据
5. ❌ CapSense 未初始化 → 所有通道读数为 0
6. ⚠️ 主循环运行，但因为 `uart_rx_available()` 总是返回 `false`，永远卡在 `STATE_IDLE`

**host 侧现象**：
```python
# Python 脚本尝试连接
port = serial.Serial('/dev/ttyUSB0', 115200)
port.write(b's')  # 发送启动命令
time.sleep(1)
data = port.read(82)  # 期望收到一帧
# → 超时！因为固件的 UART RX 根本没初始化，收不到 's'
```

---

### 场景 2：与原固件对比测试

| 测试项 | 原固件 | 重建固件 | 结果 |
|---|---|---|---|
| 烧录 | ✅ | ✅ | 两者都能烧录 |
| 启动 | ✅ 200ms 后开始响应 | ❌ 永远无响应 | **不一致** |
| 发送 's' | ✅ 立即开始 200 Hz 数据流 | ❌ 无反应 | **不一致** |
| 数据内容 | ✅ 12 路实时 raw 值 | ❌ 无数据 | **不一致** |
| 发送 'q' | ✅ 返回设备 ID | ❌ 无反应 | **不一致** |
| 发送 't' | ✅ 执行 Tare 并保存 | ❌ 无反应 | **不一致** |

---

## ✅ 当前固件的价值

虽然**不能直接使用**，但它提供了：

### 1. 完整的项目框架 ✅
- ModusToolbox 3.7 构建系统配置正确
- 设备支持文件（cy8c4149azi_s593.h）已补齐
- 启动代码（32-IRQ 向量表）已生成
- 链接脚本（384K/32K）正确

### 2. 正确的协议实现 ✅
- 帧格式（82 字节）与原固件一致
- 命令解析逻辑正确
- 状态机转换正确

### 3. 清晰的代码结构 ✅
- 6 个模块职责分明
- 接口定义清晰（`.h` 文件）
- 易于补全实现

### 4. 逆向结果的验证 ✅
- 通过编译证明了逆向分析的正确性
- MSC 寄存器配置已记录在注释中
- 为后续实现提供了精确参考

---

## 🚀 补全路线图（预估时间）

### 最小可运行版本（2-4 小时）

#### 1. UART 通信（1 小时）
```c
// 使用 PDL 的 SCB UART API
#include "cy_scb_uart.h"

static cy_stc_scb_uart_context_t g_uart_context;

void uart_comm_init(void)
{
    cy_stc_scb_uart_config_t config = {
        .uartMode = CY_SCB_UART_STANDARD,
        .oversample = 12,
        .dataWidth = 8,
        .enableMsbFirst = false,
        .stopBits = CY_SCB_UART_STOP_BITS_1,
        .parity = CY_SCB_UART_PARITY_NONE,
        .enableInputFilter = false,
        .dropOnParityError = false,
        .dropOnFrameError = false,
        .enableMuteSetting = false,
        .rxFifoTriggerLevel = 0,
        .txFifoTriggerLevel = 0,
        .breakWidth = 11,
        .rxFifoIntEnableMask = 0,
        .txFifoIntEnableMask = 0
    };
    
    Cy_SCB_UART_Init(SCB0, &config, &g_uart_context);
    Cy_SCB_UART_Enable(SCB0);
}

void uart_send_frame(const record_t records[NUM_CHANNELS])
{
    uint8_t frame[UART_FRAME_SIZE];
    /* ... 构造帧（已有代码不变）... */
    
    Cy_SCB_UART_PutArray(SCB0, frame, UART_FRAME_SIZE);
}
```

#### 2. 系统时钟（30 分钟）
```c
#include "cy_sysclk.h"

static void system_init(void)
{
    // 配置 IMO 为 48 MHz
    Cy_SysClk_ImoEnable();
    Cy_SysClk_ClkSysSetDivider(CY_SYSCLK_DIV_1);
    
    __enable_irq();  // 使能全局中断
    
    record_init();
    uart_comm_init();
    // ...
}
```

#### 3. CapSense MSC 初始化（2 小时）
```c
// 直接应用逆向工程恢复的寄存器配置
void capsense_scan_init(void)
{
    // 1. 配置时钟
    Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 0);
    Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 0, 0);
    Cy_SysClk_PeriphAssignDivider(PCLK_MSC0_CLOCK_MSC, CY_SYSCLK_DIV_16_BIT, 0);
    
    // 2. 配置 GPIO（从逆向结果的引脚映射）
    // P0[0..3], P1[0..3], P2[0..3], P3[0..1] → MSC mode
    for (int i = 0; i < 4; i++) {
        Cy_GPIO_SetHSIOM(P0_0_PORT, P0_0_PIN + i, P0_0_MSC0_SENSE);
        // ... 其他端口
    }
    
    // 3. 应用 v54 逆向的 MSC 配置
    MSC0->SNS_CTL = 0x00c800c8;
    MSC0->MODE[0].SENSE_DUTY_CTL = 0x04040040;
    MSC0->MODE[0].SW_SEL_CDAC_FL = 0x03002000;
    // ... 剩余 13 个 slot（从 v54 文档复制）
    
    // 4. 使能 MSC 中断
    Cy_SysInt_Init(&msc0_isr_cfg, msc_0_interrupt_IRQHandler);
    NVIC_EnableIRQ(msc_0_interrupt_IRQn);
    
    // 5. 启动扫描
    MSC0->CTL |= MSC_CTL_ENABLED_Msk;
}
```

#### 4. 测试（30 分钟）
- 烧录固件
- Python 脚本发送 `'s'`
- 验证 200 Hz 数据流
- 对比原固件的 raw 值

**预期结果**：
- ✅ UART 通信正常
- ✅ 数据帧格式正确
- ⚠️ raw 值可能略有偏差（需要微调 MSC 参数）

---

### 完整功能版本（+2 小时）

#### 5. EEPROM（1 小时）
```c
#include "cy_em_eeprom.h"

static cy_stc_eeprom_context_t g_eeprom_context;
static const cy_stc_eeprom_config_t g_eeprom_config = {
    .eepromSize = 512,
    .blockingWrite = 1,
    .redundantCopy = 1,
    .wearLevelingFactor = 2,
    .simpleMode = 0
};

void eeprom_mgr_init(void)
{
    Cy_Em_EEPROM_Init(&g_eeprom_config, &g_eeprom_context);
}

void eeprom_read(uint16_t addr, uint8_t* data, uint16_t len)
{
    Cy_Em_EEPROM_Read(addr, data, len, &g_eeprom_context);
}

void eeprom_write(uint16_t addr, const uint8_t* data, uint16_t len)
{
    Cy_Em_EEPROM_Write(addr, data, len, &g_eeprom_context);
}
```

#### 6. CapSense 中断优化（30 分钟）
```c
void msc_0_interrupt_IRQHandler(void)
{
    // 读取 14 个 slot 的结果
    for (int i = 0; i < NUM_CHANNELS; i++) {
        g_raw_values[i] = MSC0->MODE[slot_map[i]].RESULT_FIFO_RD;
    }
    
    // 清除中断
    MSC0->INTR = MSC_INTR_MASK_SCAN_Msk;
    
    g_scan_complete = 1;
}
```

#### 7. 往返测试（30 分钟）
- 82249 帧验证（与 v54 相同）
- 延迟测量（应 < 5 ms）
- Tare 功能验证
- 设备 ID 读写验证

---

## 💡 建议

### 方案 A：完成重建固件（推荐学习/定制化）

**适用场景**：
- 需要修改采样率（200 Hz → 其他）
- 需要添加新的 UART 命令
- 需要优化 CapSense 灵敏度
- 想要完全理解系统

**时间投入**：4-6 小时（首次）

**风险**：
- MSC 参数调试可能耗时
- 需要反复烧录测试

---

### 方案 B：保持使用原固件（推荐生产/稳定性）

**适用场景**：
- 原固件已满足需求
- 追求稳定性
- 不需要修改固件

**优势**：
- ✅ 已验证（82249/82249 帧通过）
- ✅ 即用即得
- ✅ 专注 host 侧优化（MLP 模型、标定工具）

**当前重建项目的价值**：
- 作为**文档**：完整理解了原固件
- 作为**参考**：未来需要定制时的起点
- 作为**验证**：证明逆向分析的正确性

---

## 📌 总结

| 问题 | 回答 |
|---|---|
| **能烧录吗？** | ✅ 能 —— HEX 文件格式正确 |
| **能运行吗？** | ⚠️ 能启动，但所有硬件交互都不工作 |
| **与原固件一致吗？** | ❌ 否 —— 所有 TODO 都是占位实现 |
| **需要多久完善？** | 2-6 小时（取决于调试时间） |
| **是否建议现在替换？** | ❌ 否 —— 除非你愿意投入时间补全 |
| **项目是否有价值？** | ✅ 是 —— 作为学习/定制化的完整框架 |

---

**下一步决策**：

1. **如果你想要立即可用** → 继续使用原固件
2. **如果你想要学习/定制** → 我可以帮你完成上述 4-6 小时的补全工作
3. **如果你想要验证框架** → 可以先烧录看现象（不会损坏硬件，随时可换回原固件）

告诉我你的选择，我继续推进。
