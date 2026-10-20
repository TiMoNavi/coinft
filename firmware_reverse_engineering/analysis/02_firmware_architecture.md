# CoinFT 固件架构概述

## 1. 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                      CoinFT 传感器板                          │
│                                                               │
│  ┌──────────────┐        ┌─────────────────────────────┐   │
│  │ 12 路电容电极 │───────▶│   PSoC 4 (CY8C4149AZI)      │   │
│  │ (梳状结构)   │  CSD   │                              │   │
│  └──────────────┘  Scan  │  ┌───────────────────────┐  │   │
│                            │  │  CapSense 中间件      │  │   │
│                            │  │  (MSC 硬件驱动)      │  │   │
│                            │  └───────┬───────────────┘  │   │
│                            │          │                   │   │
│                            │  ┌───────▼───────────────┐  │   │
│                            │  │  主循环状态机         │  │   │
│                            │  │  idle/streaming/blocked│ │   │
│                            │  └───────┬───────────────┘  │   │
│                            │          │                   │   │
│                            │  ┌───────▼───────────────┐  │   │
│                            │  │  UART 编码器          │  │   │
│                            │  │  26 字节帧 @ 201 Hz  │  │   │
│                            │  └───────┬───────────────┘  │   │
│                            └──────────┼───────────────────┘   │
└───────────────────────────────────────┼───────────────────────┘
                                        │ UART
                                        │ 115200 baud
                             ┌──────────▼──────────┐
                             │   Teensy 4.1 桥接    │
                             │   (双板组合)         │
                             └──────────┬──────────┘
                                        │ USB
                             ┌──────────▼──────────┐
                             │   Host 计算机        │
                             │                      │
                             │  ┌────────────────┐ │
                             │  │  MLP 模型      │ │
                             │  │  12 → 6D       │ │
                             │  │  Fx Fy Fz     │ │
                             │  │  Mx My Mz     │ │
                             │  └────────────────┘ │
                             └─────────────────────┘
```

---

## 2. 核心模块

### 2.1 CapSense 扫描引擎

**功能**：通过 MSC（Multi-Sense Converter）硬件采集 12 路电容值

**工作流程**：
```
1. 配置 MSC 寄存器（SNS_CTL, SW_SEL_*, SCAN_CTL1...）
2. 启动扫描：slot 0 → 1 → 2 → ... → 13 → LAST
3. 每个 slot 完成触发 CONFIG_REQ 中断
4. slot 13 后触发 SCAN_COMPLETE 中断
5. ISR 读取 MSC RESULT 寄存器，写入 SRAM record
```

**关键寄存器**：
- `MSC_BASE = 0x40290000`
- `SNS_CTL (+0x14)`：LFSR 模式、sense divider
- `SW_SEL_CSW (+0x30)`：电极连接开关
- `RESULT1 (+0x88)`：16-bit raw count
- `INTR (+0x170)`：中断标志（INIT/CONFIG_REQ/SCAN_COMPLETE）

**配置来源**：
- Flash 中 14 个 frame 配置（每个 0x38 字节）
- 每个 frame 包含：sense_width, cdac_sel_re, comp_divider 等
- slot 0..13 映射到 12 个物理通道（slot 6/13 是特殊配置）

### 2.2 数据结构

#### record 结构（13 条，每条 10 字节）

```c
typedef struct {
    uint16_t raw;           // +0x00: MSC 实时读数
    uint16_t baseline;      // +0x02: tare 时的快照（零点）
    uint16_t field2;        // +0x04: 恒为零（预留）
    uint16_t field3;        // +0x06: 恒为零（预留）
    uint8_t  diffcount;     // +0x08: raw > baseline 的累加计数
    uint8_t  negdiffcount;  // +0x09: raw < baseline 的累加计数
} coinft_record_t;

// 地址：0x2000045C + ch * 10
// ch=0..11: 对应 UART 帧的 12 个通道
// ch=12:    record 12，绝不上线（可能是内部校准通道）
```

**更新时机**：
- `raw`：每次 SCAN_COMPLETE 中断（~201 Hz）
- `baseline`：只在执行 `t` 命令时更新（baseline := raw）
- `diffcount/negdiffcount`：每次扫描后根据 raw-baseline 关系累加

#### history 数组（12 × 4 个 uint32）

```c
uint32_t history[12][4];  // 地址：0x200003C0

// 用途：negdiffcount 的中间累加器
// history[ch][0/2/3] 在运行中变化
// history[ch][1] 是条件写入（只在某些通道/状态下更新）
```

**作用**：可能用于检测"持续负偏差"（如电极故障、环境漂移）

### 2.3 主循环状态机

```c
enum {
    STATE_IDLE = 0,      // 初始状态，不扫描，UART 不发送
    STATE_STREAMING,     // 正常工作，扫描 + 发送 UART
    STATE_BLOCKED        // 等待命令字节，扫描停止
};

while (1) {
    if (state == STATE_STREAMING) {
        if (MSC_INTR & SCAN_COMPLETE) {
            update_records();  // 从 MSC RESULT 读取 raw
            send_uart_frame(); // 编码 26 字节发送
        }
        if (uart_rx_available()) {
            handle_command();  // 可能切换到 BLOCKED
        }
    }
    else if (state == STATE_BLOCKED) {
        // 等待足够的命令字节
        if (command_complete()) {
            execute_command();
            if (command != 'w') {
                state = STATE_STREAMING;
            }
        }
    }
    else if (state == STATE_IDLE) {
        // 等待 's' 命令启动
        if (uart_rx_available() && read_byte() == 's') {
            MSC_INTR = INIT;  // 触发初始化
            state = STATE_STREAMING;
        }
    }
}
```

**关键观察**：
- `IDLE` 时：MSC 不运行，UART 不发送，record 不更新
- `STREAMING` 时：扫描 + 发送同步，约 201 Hz
- `BLOCKED` 时：扫描停止，UART **继续发送陈旧数据**（上次的 record）

### 2.4 命令协议

| 命令 | 字节数 | 参数 | 功能 | 阻塞行为 |
|---|---|---|---|---|
| `i` | 1 | 无 | idle：停止扫描和发送 | 立即返回 |
| `s` | 1 | 无 | streaming：启动扫描 | 立即返回 |
| `t` | 4 | index, param[2] | tare：baseline := raw（**无条件**） | 阻塞 4 字节 |
| `q` | 1 | 无 | query：回复 0x1A | 立即返回，回 1 字节 |
| `w` | 变长 | device_id... | write：写 Flash，直落 `n` | 阻塞到 CR 或 12 字节 |
| `n` | 1 | 无 | name：读设备 ID | 立即返回，回 12 字节 |

**阻塞语义**：
- `t`：读满 4 字节后才返回 STREAMING
- `w`：读到 `\r` (0x0D) 或 12 字节后写 Flash，**不返回 STREAMING**，直接执行 `n` 逻辑

**关键发现（v51）**：
- `t 0xFF 0x00 0x00` 虽然拒绝写入扫描参数（index 无效），但仍会 **tare 全部 13 条 record**
- 这是"非原子性"设计：tare 与参数写入是两个独立步骤

### 2.5 UART 帧格式

```
┌─────┬──────┬──────┬─────┬──────┬─────┬──────┐
│ SOF │ CH0  │ CH1  │ ... │ CH11 │ ... │ EOF  │
│ 0x02│ u16LE│ u16LE│     │ u16LE│     │ 0x03 │
└─────┴──────┴──────┴─────┴──────┴─────┴──────┘
  1B     2B     2B    ...    2B    ...    1B

总计：26 字节
```

**编码**：
- SOF (Start of Frame): `0x02`
- 12 个通道：`record[0..11].raw`，小端序 uint16
- EOF (End of Frame): `0x03`
- 无 CRC 校验

**发送频率**：
- 约 201 Hz（实测）
- 与扫描周期同步（SCAN_COMPLETE 后立即发送）

**特殊行为**：
- **BLOCKED 状态下继续发送陈旧数据**（上次 record 的快照）
- **record 12 绝不上线**（UART 只发送 ch 0..11）

### 2.6 Em_EEPROM（Flash 存储）

**用途**：持久化设备 ID 和扫描参数

**地址**：`0x00005000`（Flash 区域）

**结构**：
```
+0x00: magic / header
+0x08: 设备 ID 条目地址
+0x0c: 设备 ID 条目长度（12 字节）
+0x10: 设备 ID 数据（如 "2606602_0019"）
+0x20: 扫描参数条目（可能包含 14 个 frame 配置）
```

**操作**：
- `w` 命令：写设备 ID → Flash 写入计数 +1
- `w` 后直落 `n`：再次读取 → Flash 写入计数 +1
- 总计：一次 `w` 命令触发 **2 次 Flash 操作**

**验证**（v51）**：
- 实测：发送 `w 1234567890ab\r` 后，SWD 读取 Flash 区域
- 看到：`+0x10` 处变为 `"1234567890ab"`
- 计数：2 次 Flash 写入

---

## 3. 关键算法

### 3.1 baseline 更新（tare 机制）

**代码逻辑**（伪代码）：
```c
void coinft_tare(uint8_t index, uint16_t param) {
    // 步骤 1：尝试写入扫描参数（可能失败）
    if (index < 13) {
        write_scan_params(index, param);
    }
    
    // 步骤 2：无条件 tare（即使 index 无效）
    for (int ch = 0; ch < 13; ch++) {
        record[ch].baseline = record[ch].raw;  // 零点校准
    }
}
```

**特点**：
- **无条件**：即使 index=0xFF 被拒，baseline 仍更新
- **不跟随**：baseline 只在 `t` 命令时更新，绝不自动跟随 raw
- **全部更新**：一次 tare 影响全部 13 条 record

**影响**：
- 用户必须在"零力状态"下执行 tare，否则会引入偏移
- 如果环境温度变化导致 raw 漂移，需要重新 tare

### 3.2 diffcount / negdiffcount 累加

**代码逻辑**：
```c
void update_diff_counters(void) {
    for (int ch = 0; ch < 13; ch++) {
        int16_t diff = record[ch].raw - record[ch].baseline;
        
        if (diff > 0) {
            if (record[ch].diffcount < 255) {
                record[ch].diffcount++;
            }
        }
        else if (diff < 0) {
            if (record[ch].negdiffcount < 255) {
                record[ch].negdiffcount++;
            }
            // 可能还会更新 history 数组
        }
    }
}
```

**用途**（推测）：
- `diffcount`：检测"持续正偏差"（如有物体压在传感器上）
- `negdiffcount`：检测"持续负偏差"（如电极故障、环境漂移）
- 但当前固件**不使用这些计数做任何决策**，只是累加

### 3.3 MSC 配置生成

**输入**：Flash 中的 14 个 frame 配置（每个 0x38 字节）

**输出**：10+ 个 MSC 寄存器的写入值

**关键映射**（v42 确认）：
```
frame.sense_width       → MSC_SNS_CTL (bit0..15)
frame.cdac_sel_re       → MSC_SW_SEL_CSW (bit10..15)
frame.comp_divider      → MSC_SCAN_CTL1 (bit16..23)
frame.lfsr_mode         → MSC_SNS_CTL (bit25..26)
frame.sub_conversions   → MSC_SCAN_CTL1 (bit0..7)
```

**未完全闭合**（v54 遗留）：
- `limit_word` 与 `cdac_sel_re` 的精确关系
- 但这不影响重建：直接复制已恢复的寄存器写入序列即可

---

## 4. 时序与同步

### 4.1 扫描周期

```
slot 0 ──▶ slot 1 ──▶ ... ──▶ slot 13 ──▶ LAST ──▶ slot 0
   │         │                   │          │
   └─────────┴───────────────────┴──────────┘
          约 5 ms（201 Hz）
```

**中断触发点**：
- **CONFIG_REQ**：每个 slot 完成后触发（用于配置下一个 slot）
- **SCAN_COMPLETE**：slot 13 完成后触发（用于读取结果并发送 UART）
- **INIT**：软件触发（启动扫描序列）

### 4.2 UART 发送时序

```
SCAN_COMPLETE IRQ ──▶ update_records() ──▶ send_uart_frame()
                          (~100 µs)            (~2 ms @ 115200)
```

**观察**（v46/v52）：
- UART 发送与扫描周期同步
- 实测频率：约 201 Hz（理论值可能是 200 Hz，误差来自时钟偏差）
- **无帧间填充**：26 字节帧之间直接相连，无额外字节

### 4.3 SWD 读取与 UART 的相位关系

**问题**：SWD 读取 SRAM（33 字节）需要 ~10 ms，跨越了 201 Hz 的扫描周期

**结果**：
- 12.2% 的 SWD 读取能精确匹配 UART 帧（v52 测试 A）
- 87.8% 是"撕裂读"（读取过程中 record 被更新）

**解决方案**：
- 测试 A 不可靠（有筛选偏差）
- 测试 B（往返测试）直接走 UART 字节流，无 SWD 参与 → 82249/82249 通过

---

## 5. 硬件依赖

### 5.1 MSC（Multi-Sense Converter）

**作用**：PSoC 4 的电容扫描硬件加速器

**工作原理**：
1. 通过 GPIO 开关矩阵连接电极
2. 对电容充电/放电
3. 用 IDAC（可编程电流源）和 sigma-delta ADC 测量电容值
4. 输出 16-bit raw count（单位：内部时钟周期数）

**配置复杂度**：
- 10+ 个寄存器，每个寄存器有数十个位域
- 不同的扫描模式（CSD, CSX, BIST）
- 本项目只使用 CSD（Capacitive Sensing - Self-Capacitance）

### 5.2 GPIO 复用

**P2[0..5]**：MSC 电极连接  
**P3[0..7]**：MSC 电极连接（部分引脚）  
**P1[0]**：UART TX  
**P1[1]**：UART RX  
**P0[4]**：可能是调试 GPIO（未使用）

**开关矩阵**：
- `SW_SEL_CSW`：选择哪些引脚连接到 Cmod（外部电容）
- `SW_SEL_GPIO`：选择哪些引脚连接到传感器电极

### 5.3 外部元件

**Cmod**：外部调制电容（约 2.2 nF，具体值未确认）  
**Csh**：shield 电容（用于降低噪声）  
**12 个电极**：梳状结构，分布在 PCB 上（具体布局需 Gerber 文件）

---

## 6. 与 host 侧的交互

### 6.1 数据流

```
CoinFT 固件：采集 12 路 raw
    ↓ UART 26 字节帧 @ 201 Hz
Teensy 桥接：组合双板数据（2 × 26 = 52 字节，加包头 = 58 字节）
    ↓ USB @ 1 kHz
Host 计算机：
    1. 接收 58 字节包
    2. 拆分为左右板各 12 路
    3. 每板独立 tare / z-score 归一化
    4. 喂入 MLP（12 输入 → 6 输出）
    5. 得到 Fx Fy Fz Mx My Mz
```

### 6.2 MLP 模型架构

```
输入层：12 个 z-score 归一化的 raw counts
    ↓
隐层 1：Linear(12 → 128) + ReLU
    ↓
隐层 2：Linear(128 → 64) + ReLU
    ↓
输出层：Linear(64 → 6)
    ↓
输出：Fx, Fy, Fz, Mx, My, Mz（未归一化）
```

**训练数据**：
- 使用 ATI 六维力传感器作为标签
- 在不同位置/方向施加已知力
- 记录 12 路 raw + 6D wrench

**Jacobian 分析**（v53）：
- Fz（法向力）主导 11/12 通道（敏感度 0.008~0.016）
- Fx/Fy（剪切力）低 10 倍
- c11 是"万能通道"

### 6.3 固件不负责的部分

**固件只做**：
- 采集 12 路 raw
- tare（零点校准）
- 编码 UART 帧

**固件不做**：
- ❌ raw → 力的换算（在 MLP 中）
- ❌ 滤波/平滑（host 侧可选）
- ❌ 温度补偿（未启用）
- ❌ 自动 tare（需要 host 侧发 `t` 命令）

---

## 7. 设计特点与权衡

### 7.1 优点

✅ **简单可靠**：状态机只有 3 个状态，逻辑清晰  
✅ **实时性好**：201 Hz 采样率，延迟低  
✅ **易于调试**：UART 协议简单，无加密无压缩  
✅ **baseline 稳定**：不自动跟随，避免漂移累积

### 7.2 缺点

⚠️ **baseline 不自动更新**：环境变化后需要手动 tare  
⚠️ **阻塞时发陈旧数据**：可能误导 host 侧（以为是新数据）  
⚠️ **无 CRC 校验**：UART 传输错误无法检测  
⚠️ **tare 非原子性**：index 无效时仍执行 tare，可能引起混淆

### 7.3 未使用的功能

🔲 **SAR（17 路 ADC）**：已初始化但 `chan_en=0`，未使用  
🔲 **field2/field3**：record 中预留，但恒为零  
🔲 **触摸检测**：CapSense 支持，但固件未启用  
🔲 **温度补偿**：SAR 可能用于此，但未激活

---

## 8. 下一步

- **查看伪代码** → [03_pseudocode_final.c](03_pseudocode_final.c)
- **重建固件** → [04_reconstruction_guide.md](04_reconstruction_guide.md)
- **查看验证结果** → [05_verification_results.md](05_verification_results.md)
