# CoinFT 固件逆向工程交付包

**版本：v54 Final（2026-08-30）**  
**状态：六维度全部达标，82249/82249 帧验证通过**

本目录包含 CoinFT 电容式力传感器固件的完整逆向工程成果，用于重新构建功能等价的固件。

---

## 目录结构

```
09_deliverables/
├── README.md                          # 本文件
├── 01_project_background.md           # 项目背景与目标
├── 02_firmware_architecture.md        # 固件架构概述
├── 03_pseudocode_final.c             # 最终伪代码（带注释）
├── 04_reconstruction_guide.md         # 重建固件指南
├── 05_verification_results.md         # 验证结果汇总
├── 06_known_limitations.md            # 已知限制与不可恢复项
└── assets/
    ├── register_maps/                 # 寄存器映射表
    ├── protocol_specs/                # 协议规范
    └── test_data/                     # 验证数据集
```

---

## 快速导航

### 如果你想要...

- **了解项目背景** → [01_project_background.md](01_project_background.md)
- **理解固件工作原理** → [02_firmware_architecture.md](02_firmware_architecture.md)
- **查看可编译的伪代码** → [03_pseudocode_final.c](03_pseudocode_final.c)
- **重新构建固件** → [04_reconstruction_guide.md](04_reconstruction_guide.md)
- **查看验证结果** → [05_verification_results.md](05_verification_results.md)
- **了解限制** → [06_known_limitations.md](06_known_limitations.md)

---

## 关键成果

### 已恢复的内容

✅ **控制流**（D1）：151/151 函数，无未识别函数  
✅ **寄存器访问**（D2）：MSC/GPIO/UART/PCLK/Em_EEPROM 位域级精确  
✅ **数据结构**（D3）：record/history/frame/EEPROM 字节级确认  
✅ **协议**（D4）：6 个命令全闭合，含阻塞语义与 Flash 副作用  
✅ **行为等价**（D5）：82249/82249 帧无筛选验证，0 次重同步  
✅ **限制说明**（D6）：不可恢复项已明确列出  
✅ **应用层映射**（D7）：12 通道 → 6D wrench 的 MLP Jacobian 已提取

### 验证规模

- **UART 帧往返测试**：82249/82249（100%）
- **MSC 配置快照**：14/14 帧逐字段匹配
- **命令状态机**：6/6 命令行为确认
- **Flash 操作**：Em_EEPROM 读写计数验证
- **时间跨度**：2026-06 至 2026-08，迭代 54 轮

---

## 硬件信息

- **主芯片**：Infineon PSoC 4 (CY8C4149AZI-S593)
- **传感器**：12 路电容式力感应电极（CSD 扫描）
- **通信**：UART 115200 baud, 26 字节帧 (0x02 + 12×uint16LE + 0x03)
- **存储**：Em_EEPROM（Flash 0x5000 区，用于设备 ID 和扫描参数）
- **采样率**：约 201 Hz（实测）

---

## 软件工具链

### 已使用的逆向工具
- **Ghidra 11.1.2**：反汇编与反编译
- **OpenOCD 4.0**：SWD 调试与 Flash 读写
- **KitProg3**：硬件调试器
- **Python 3.11**：自动化验证脚本

### 重建固件所需
- **ModusToolbox 3.0+**：Infineon 官方 IDE
- **PSoC Creator 4.4+**（可选）：GUI 配置工具
- **ARM GCC 10.3+**：编译器
- **详见** → [04_reconstruction_guide.md](04_reconstruction_guide.md)

---

## 使用场景

### 1. 完全重建固件
从零编写一个功能等价的新固件，用于：
- 修改采样率、滤波算法
- 添加新的 UART 命令
- 适配不同的 PSoC 4 型号

**参考**：[04_reconstruction_guide.md](04_reconstruction_guide.md)

### 2. 理解现有固件行为
在不修改固件的情况下，理解其工作原理：
- 12 路 raw 如何生成
- baseline 如何更新（tare 机制）
- UART 帧如何编码

**参考**：[02_firmware_architecture.md](02_firmware_architecture.md)

### 3. 扩展上层应用
基于已知的 UART 协议和 MLP 映射：
- 优化 12 路 → 6D wrench 的神经网络
- 开发新的标定流程
- 实现多传感器融合

**参考**：[05_verification_results.md](05_verification_results.md) 的 MLP 映射部分

---

## 联系与支持

本逆向工程由 Claude Opus 5 完成（2026-06 至 2026-08）。

如有疑问，请参考：
- 完整过程记录：`../07_analysis/restoration_progress_v54.md`
- 事实登记表：`../07_analysis/fact_registry_v48.json`
- 测试套件：`../10_reconstruction_v13/tests/`

---

**最后更新**：2026-08-30  
**版本**：v54（最终交付版）
