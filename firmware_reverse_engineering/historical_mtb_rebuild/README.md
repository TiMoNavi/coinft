# CoinFT Firmware Rebuild (ModusToolbox)

基于逆向工程 v54 结果重建的固件。

## 状态

- ✅ 项目结构
- ✅ 源代码（6 个模块）
- ⚠️ 需要 PSoC 4 HAL/BSP 库
- ⚠️ 需要完善启动代码和链接器脚本

## 构建

```bash
make getlibs  # 获取依赖（需要网络）
make build    # 编译
make program  # 烧录
```

## 依赖

- ModusToolbox 3.7+
- PSoC 4 HAL
- PSoC 4 PDL
- CapSense Middleware（可选，我们使用寄存器直接控制）

## 当前问题

1. 启动代码和链接器脚本是占位符，需要从 Infineon BSP 获取完整版本
2. UART/EEPROM 实现需要 PSoC HAL
3. 由于网络问题，无法自动下载依赖

## 解决方案

手动下载并放置 PSoC 4 BSP：
```bash
# 从其他 ModusToolbox 项目复制
# 或从 GitHub 手动下载
```
