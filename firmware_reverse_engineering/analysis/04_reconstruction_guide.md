# CoinFT 固件重建指南

## 1. 概述

本文档指导如何从零重新构建功能等价的 CoinFT 固件。

**前提条件**：
- 已阅读 [01_project_background.md](01_project_background.md) 和 [02_firmware_architecture.md](02_firmware_architecture.md)
- 理解 PSoC 4 架构和 CapSense 工作原理
- 熟悉 C 语言和嵌入式开发

**目标**：
- 生成能烧录到 PSoC 4 的 HEX 文件
- UART 输出与原始固件逐字节一致（给定相同 raw 输入）
- 支持全部 6 个命令（i/s/t/q/w/n）

---

## 2. 开发环境准备

### 2.1 必需软件

| 软件 | 版本 | 用途 | 下载 |
|---|---|---|---|
| **ModusToolbox** | 3.0+ | Infineon 官方 IDE | [infineon.com](https://www.infineon.com/modustoolbox) |
| **PSoC Creator** | 4.4+ (可选) | GUI 配置 CapSense | [infineon.com](https://www.infineon.com/psoccreator) |
| **ARM GCC** | 10.3+ | 编译器（ModusToolbox 自带） | - |
| **OpenOCD** | 4.0+ | 烧录与调试 | ModusToolbox 自带 |
| **Python** | 3.8+ | 验证脚本 | [python.org](https://www.python.org/) |

### 2.2 硬件工具

| 工具 | 用途 | 替代方案 |
|---|---|---|
| **KitProg3** | SWD 调试器 | MiniProg4, J-Link |
| **UART 转 USB** | 读取 26 字节帧 | FT232, CP2102 |
| **示波器/逻辑分析仪** | 可选，验证时序 | - |

### 2.3 环境变量（macOS/Linux）

```bash
# ModusToolbox 安装路径
export MODUSTOOLBOX_PATH="/Applications/ModusToolbox"
export PATH="$MODUSTOOLBOX_PATH/tools_3.0/gcc/bin:$PATH"

# OpenOCD
export OPENOCD="$MODUSTOOLBOX_PATH/tools_3.0/openocd/bin/openocd"
export OPENOCD_SCRIPTS="$MODUSTOOLBOX_PATH/tools_3.0/openocd/scripts"
```

---

## 3. 项目初始化

### 3.1 使用 ModusToolbox（推荐）

```bash
# 1. 创建新项目
cd ~/workspace
$MODUSTOOLBOX_PATH/tools_3.0/project-creator/project-creator-cli \
  --board-id CY8CKIT-149 \
  --app-id mtb-example-psoc4-empty-app \
  --user-app-name coinft_v2_firmware

cd coinft_v2_firmware

# 2. 验证工具链
make getlibs
make build
```

### 3.2 使用 PSoC Creator（可选，用于 GUI 配置）

1. 打开 PSoC Creator
2. **File → New → Project**
3. 选择 **PSoC 4 → CY8C4149AZI-S593**
4. 模板：**Empty PSoC 4 Design**
5. 项目名：`coinft_v2_firmware`

---

## 4. CapSense 配置

### 4.1 方法 A：从逆向结果重建（推荐）

**直接写寄存器，不依赖 CapSense 中间件**

```c
// 参考 03_pseudocode_final.c 的寄存器写入序列
#define MSC0_BASE 0x40290000
#define MSC_SNS_CTL     (MSC0_BASE + 0x14)
#define MSC_SCAN_CTL1   (MSC0_BASE + 0x50)
#define MSC_SW_SEL_CSW  (MSC0_BASE + 0x30)
// ... 更多寄存器

void capsense_init(void) {
    // 从 v18/v42 恢复的寄存器序列
    *(volatile uint32_t*)MSC_SNS_CTL = 0x00C800C8;
    *(volatile uint32_t*)MSC_SCAN_CTL1 = 0x00000400;
    // ... 14 个 slot 的配置
}
```

**优点**：
- 完全控制，无黑盒依赖
- 可以精确复现原始固件的寄存器值

**缺点**：
- 需要手动管理所有寄存器
- 难以适配不同的 PSoC 型号

### 4.2 方法 B：使用 CapSense Tuner（简化版）

**用 PSoC Creator 的 GUI 工具配置**

1. 在 TopDesign.cysch 中添加 **CapSense Component**
2. 配置参数：
   - **Sensing Mode**: CSD（Self-Capacitance）
   - **Number of Sensors**: 12
   - **Scan Resolution**: 12-bit
   - **Modulator Clock**: 约 48 MHz / sense_width
   - **Sense Clock Divider**: 从 v42 的 `comp_divider` 推算

3. 生成代码：**Build → Generate Application**

4. 修改生成的 `CapSense_Sensing.c`：
   - 移除触摸检测逻辑
   - 只保留 raw count 读取

**优点**：
- GUI 友好，易于调整参数
- 自动处理时钟配置

**缺点**：
- 生成的代码可能与原始固件不完全一致
- 需要额外的参数调优

### 4.3 关键参数对照表

| 伪代码变量 | CapSense Tuner 参数 | 典型值 |
|---|---|---|
| `sense_width` | Sense Clock Divider | 8449 |
| `cdac_sel_re` | Reference IDAC | 48 |
| `comp_divider` | Modulator Clock Divider | varies |
| `sub_conversions` | Number of Sub-Conversions | varies |

**注意**：这些值在 14 个 slot 中不同，需要逐个配置。

---

## 5. 核心模块实现

### 5.1 主循环状态机

```c
typedef enum {
    STATE_IDLE = 0,
    STATE_STREAMING,
    STATE_BLOCKED
} system_state_t;

volatile system_state_t state = STATE_IDLE;

int main(void) {
    system_init();
    uart_init(115200);
    capsense_init();
    
    while (1) {
        switch (state) {
            case STATE_IDLE:
                if (uart_rx_available() && uart_read_byte() == 's') {
                    capsense_start();
                    state = STATE_STREAMING;
                }
                break;
            
            case STATE_STREAMING:
                if (capsense_scan_complete()) {
                    update_records();
                    send_uart_frame();
                }
                if (uart_rx_available()) {
                    handle_command();
                }
                break;
            
            case STATE_BLOCKED:
                if (command_buffer_ready()) {
                    execute_command();
                    if (last_command != 'w') {
                        state = STATE_STREAMING;
                    }
                }
                break;
        }
    }
}
```

### 5.2 record 结构

```c
typedef struct {
    uint16_t raw;
    uint16_t baseline;
    uint16_t field2;      // 保留，写为 0
    uint16_t field3;      // 保留，写为 0
    uint8_t  diffcount;
    uint8_t  negdiffcount;
} coinft_record_t;

coinft_record_t records[13];  // 13 条，最后一条不上线
```

### 5.3 UART 编码

```c
void send_uart_frame(void) {
    uint8_t frame[26];
    frame[0] = 0x02;  // SOF
    
    for (int i = 0; i < 12; i++) {
        uint16_t raw = records[i].raw;
        frame[1 + i*2]     = raw & 0xFF;        // 低字节
        frame[1 + i*2 + 1] = (raw >> 8) & 0xFF; // 高字节
    }
    
    frame[25] = 0x03;  // EOF
    
    uart_send_bytes(frame, 26);
}
```

### 5.4 tare 实现

**关键：无条件更新 baseline**

```c
void coinft_tare(uint8_t index, uint16_t param) {
    // 步骤 1：尝试写入扫描参数（可能失败）
    if (index < 13) {
        // 写入 Flash 配置段或 SRAM 临时缓冲区
        write_scan_params(index, param);
    }
    
    // 步骤 2：无条件 tare（即使 index 无效）
    for (int ch = 0; ch < 13; ch++) {
        records[ch].baseline = records[ch].raw;
    }
}
```

### 5.5 命令解析

```c
void handle_command(void) {
    uint8_t cmd = uart_read_byte();
    
    switch (cmd) {
        case 'i':
            capsense_stop();
            state = STATE_IDLE;
            break;
        
        case 's':
            capsense_start();
            state = STATE_STREAMING;
            break;
        
        case 't':
            state = STATE_BLOCKED;
            command_buffer_size = 4;  // 等待 4 字节
            break;
        
        case 'q':
            uart_send_byte(0x1A);
            break;
        
        case 'w':
            state = STATE_BLOCKED;
            command_buffer_size = 12;  // 或等到 CR
            break;
        
        case 'n':
            send_device_id();
            break;
    }
}

void execute_command(void) {
    if (last_command == 't') {
        uint8_t index = command_buffer[0];
        uint16_t param = (command_buffer[2] << 8) | command_buffer[1];
        coinft_tare(index, param);
    }
    else if (last_command == 'w') {
        write_device_id(command_buffer, command_buffer_len);
        send_device_id();  // 直落 'n'
    }
}
```

### 5.6 Em_EEPROM 操作

```c
#include "cy_em_eeprom.h"

#define EEPROM_LOGICAL_SIZE 128
#define EEPROM_PHYSICAL_SIZE 256

cy_stc_eeprom_context_t eeprom_context;

void eeprom_init(void) {
    cy_stc_eeprom_config_t config = {
        .eepromSize = EEPROM_LOGICAL_SIZE,
        .blockingWrite = true,
        .redundantCopy = true,
        .wearLevelingFactor = 2,
        .simpleMode = false,
    };
    
    Cy_Em_EEPROM_Init(&config, &eeprom_context);
}

void write_device_id(const uint8_t* data, uint8_t len) {
    // 写入偏移 0x10（根据原始固件的地址）
    Cy_Em_EEPROM_Write(0x10, data, len, &eeprom_context);
}

void read_device_id(uint8_t* buffer) {
    Cy_Em_EEPROM_Read(0x10, buffer, 12, &eeprom_context);
    
    // 如果未初始化，写入默认值
    if (buffer[0] == 0xFF) {
        const uint8_t default_id[] = "COINFT_V2_00";
        write_device_id(default_id, 12);
        memcpy(buffer, default_id, 12);
    }
}
```

---

## 6. 编译与烧录

### 6.1 编译（ModusToolbox）

```bash
cd coinft_v2_firmware
make build -j8

# 检查输出
ls -lh build/CY8C4149AZI-S593/Release/coinft_v2_firmware.hex
```

### 6.2 烧录（OpenOCD + KitProg3）

```bash
# 1. 连接 KitProg3 到 PSoC 4 板子

# 2. 烧录
$OPENOCD -s $OPENOCD_SCRIPTS \
  -f interface/kitprog3.cfg \
  -c "set PSOC4_USE_ACQUIRE 0" \
  -f target/psoc4.cfg \
  -c "program build/CY8C4149AZI-S593/Release/coinft_v2_firmware.hex verify reset exit"
```

### 6.3 验证烧录

```bash
# 读回 Flash 并与 HEX 比较
$OPENOCD -s $OPENOCD_SCRIPTS \
  -f interface/kitprog3.cfg \
  -c "set PSOC4_USE_ACQUIRE 0" \
  -f target/psoc4.cfg \
  -c "init; flash read_bank 0 readback.bin 0 0x20000; verify_image build/.../coinft_v2_firmware.hex; shutdown"
```

---

## 7. 验证与调试

### 7.1 UART 输出测试

```python
import serial
import struct

ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)

# 1. 启动扫描
ser.write(b's')
time.sleep(0.5)

# 2. 读取帧
while True:
    data = ser.read(26)
    if len(data) == 26 and data[0] == 0x02 and data[25] == 0x03:
        # 解析 12 路 raw
        raw = [struct.unpack('<H', data[1+i*2:3+i*2])[0] for i in range(12)]
        print(f"Raw: {raw}")
    else:
        print(f"无效帧: {data.hex()}")
```

### 7.2 命令测试

```python
# tare
ser.write(b't\x00\x00\x00')
time.sleep(0.1)

# query
ser.write(b'q')
response = ser.read(1)
assert response == b'\x1a', f"query 响应错误: {response.hex()}"

# write device ID
ser.write(b'w1234567890ab\r')
time.sleep(0.2)

# name
ser.write(b'n')
device_id = ser.read(12)
print(f"Device ID: {device_id.decode('utf-8', errors='ignore')}")
```

### 7.3 往返验证

**使用逆向工程的模拟器验证**

```bash
cd archives/reverse_engineering_coinft_legacy_20260903/10_reconstruction_v13/tools

# 1. 采集新固件的 UART 输出
python3 - <<'EOF'
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200)
ser.write(b's'); time.sleep(5)
with open('/tmp/new_firmware_capture.bin', 'wb') as f:
    f.write(ser.read(26 * 1000))  # 1000 帧
EOF

# 2. 往返测试
cc sim_roundtrip_v52.c -o simrt
./simrt /tmp/new_firmware_capture.bin

# 预期输出：roundtrip_identical=1000/1000
```

---

## 8. 常见问题

### 8.1 UART 无输出

**可能原因**：
- 未发送 `s` 命令启动扫描
- GPIO 复用配置错误（UART TX 引脚未配置）
- 波特率不匹配

**调试**：
```c
// 在 main() 开头添加调试输出
uart_send_string("CoinFT V2 Firmware\r\n");
```

### 8.2 raw 值异常（全 0 或 65535）

**可能原因**：
- MSC 配置错误
- 电极未连接或短路
- Cmod 电容缺失

**调试**：
- 用 SWD 读取 MSC_RESULT1 寄存器（0x40290088）
- 检查 MSC_INTR（0x40290170）是否有 SCAN_COMPLETE 标志

### 8.3 tare 后 baseline 未更新

**可能原因**：
- 忘记无条件 tare（即使 index 无效）
- baseline 写入了错误的 SRAM 地址

**调试**：
```c
// 在 tare 后打印 baseline
for (int i = 0; i < 12; i++) {
    printf("ch%d baseline=%u\n", i, records[i].baseline);
}
```

### 8.4 与原始固件 UART 输出不一致

**可能原因**：
- MSC 配置参数差异（sense_width, cdac_sel_re 等）
- 扫描顺序错误（slot 映射不对）
- raw 读取时机错误（在中断中还是主循环）

**解决**：
- 用 SWD 对比原始固件和新固件的 MSC 寄存器快照
- 用逻辑分析仪对比 UART 时序

---

## 9. 优化与扩展

### 9.1 添加 CRC 校验

```c
uint8_t crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

void send_uart_frame_with_crc(void) {
    uint8_t frame[27];  // 26 + 1 (CRC)
    frame[0] = 0x02;
    
    for (int i = 0; i < 12; i++) {
        uint16_t raw = records[i].raw;
        frame[1 + i*2] = raw & 0xFF;
        frame[2 + i*2] = (raw >> 8) & 0xFF;
    }
    
    frame[25] = 0x03;
    frame[26] = crc8(frame, 26);
    
    uart_send_bytes(frame, 27);
}
```

### 9.2 启用 SAR（温度监控）

```c
#define SAR0_BASE 0x403A0000
#define SAR_SAMPLE_CTRL (SAR0_BASE + 0x40)
#define SAR_CHAN_RESULT (SAR0_BASE + 0x180)

void sar_init(void) {
    // 使能通道 16（内部温度传感器）
    *(volatile uint32_t*)SAR_SAMPLE_CTRL = 0x00010000;
    // 启动转换...
}

uint16_t read_temperature(void) {
    // 读取通道 16 结果
    uint32_t result = *(volatile uint32_t*)(SAR_CHAN_RESULT + 16*4);
    return result & 0xFFF;  // 12-bit
}
```

### 9.3 自动 tare（环境自适应）

```c
// 在 main loop 中添加
static uint32_t last_tare_time = 0;
if (millis() - last_tare_time > 60000) {  // 每 60 秒
    if (is_idle_state()) {  // 判断无外力
        coinft_tare(0, 0);
        last_tare_time = millis();
    }
}
```

---

## 10. 性能基准

### 10.1 目标指标

| 指标 | 目标值 | 验证方法 |
|---|---|---|
| **UART 帧率** | 200-202 Hz | 逻辑分析仪测量帧间隔 |
| **扫描延迟** | < 5 ms | SCAN_COMPLETE 到 UART SOF |
| **命令响应** | < 10 ms | 发送 `q` 到收到 0x1A |
| **Flash 写入** | < 50 ms | `w` 命令完成时间 |
| **功耗（active）** | < 20 mA @ 3.3V | 万用表测量 |

### 10.2 与原始固件对比

| 项目 | 原始固件 | 重建固件 | 差异 |
|---|---|---|---|
| Flash 占用 | ~18 KB | 预计 15-20 KB | ±10% |
| SRAM 占用 | ~2 KB | 预计 2-3 KB | ±20% |
| UART 帧率 | 201.3 Hz | 目标 201±1 Hz | < 1% |
| 帧往返测试 | 82249/82249 | 目标 100% | 0 差异 |

---

## 11. 交付清单

完成重建后，应包含：

- [ ] **源代码**：完整的 C 工程，含 Makefile
- [ ] **HEX 文件**：可直接烧录的固件
- [ ] **配置文件**：CapSense 参数（如用 PSoC Creator）
- [ ] **验证报告**：UART 输出与原始固件的对比
- [ ] **测试脚本**：Python 脚本验证 6 个命令
- [ ] **README**：构建步骤和已知限制

---

## 12. 参考资料

### 12.1 官方文档

- **PSoC 4 Architecture TRM**：寄存器定义和时序图
- **CY8C4149 Datasheet**：引脚定义和电气特性
- **CapSense Design Guide**：AN64846（电极设计）
- **Em_EEPROM Middleware**：API 参考

### 12.2 本项目文档

- **伪代码**：[03_pseudocode_final.c](03_pseudocode_final.c)
- **架构文档**：[02_firmware_architecture.md](02_firmware_architecture.md)
- **验证结果**：[05_verification_results.md](05_verification_results.md)
- **寄存器映射**：`assets/register_maps/`（待生成）

### 12.3 社区资源

- **Infineon Community**：https://community.infineon.com/
- **PSoC Creator Examples**：IDE 自带示例工程
- **Cypress GitHub**：https://github.com/Infineon

---

## 13. 支持

如遇到问题，请：
1. 检查本文档的"常见问题"章节
2. 查阅完整的逆向工程记录：`../07_analysis/restoration_progress_v54.md`
3. 运行验证脚本：`../10_reconstruction_v13/tools/validate_facts_v48.py`

**关键提示**：重建固件时，优先保证**行为等价**（UART 输出一致），而非**代码等价**（源码长得像）。只要 82249/82249 帧往返测试通过，固件就是正确的。
