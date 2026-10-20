# CoinFT firmware reverse engineering archive

这份归档整理了 CoinFT V2 原始固件、反编译材料、重建候选代码和后续编译版本。文件从本地研究目录选择性复制，保留了来源与用途说明；没有包含完整 Ghidra 工程、采集原始数据或第三方工具库。

## 先找 HEX

| 文件 | 类型 | 大小 | 说明 |
| --- | --- | ---: | --- |
| [`original_firmware/CoinFT_V2_firmware.hex`](original_firmware/CoinFT_V2_firmware.hex) | 原始固件 | 63,976 B | 逆向分析的输入镜像，不是反编译结果。SHA-256：`615d38614e587bbb03512ab7565b7451839bd992a490ec1541d70e0ba2249725` |
| [`historical_mtb_rebuild/build/coinft_firmware_rebuild_mtb-debug.hex`](historical_mtb_rebuild/build/coinft_firmware_rebuild_mtb-debug.hex) | 历史重编译 | 4,364 B | 较早的 ModusToolbox 重建输出。历史实验版本，状态报告对完整度有不同描述，不能视作已验证的等价固件。 |
| [`stage43_maintenance/build/out/43_maintenance_commands.hex`](stage43_maintenance/build/out/43_maintenance_commands.hex) | Stage 43 | 57,321 B | 维护命令版本；有受保护硬件测试记录。说明和限制见 [Stage 43 README](stage43_maintenance/README.md)。 |
| [`stage44_integer_mlp/build/out/44_integer_mlp_experiment.hex`](stage44_integer_mlp/build/out/44_integer_mlp_experiment.hex) | Stage 44 实验 | 104,698 B | 加入整数 MLP 的实验镜像，不是生产标定输出。说明见 [Stage 44 README](stage44_integer_mlp/README.md)。 |

## 目录

- `original_firmware/`：原始 HEX 和与之对应的 BIN。
- `analysis/`：交付文档、最终伪代码、字符串、内存映射、反汇编和进度/事实记录。
- `reconstruction_v13/`：主机侧验证用的重建候选源码及工具。README 明确说明它不产生可烧录镜像。
- `historical_mtb_rebuild/`：早期 ModusToolbox 重建代码、关键状态说明和编译输出；不包含厂商依赖库。
- `stage43_maintenance/`：Stage 43 源码、验证工具和编译输出。
- `stage44_integer_mlp/`：Stage 44 源码、测试、量化资料和编译输出。

## 来源与限制

这些文件来自 `/Users/550m/code/UMIFT-datacollect/modules/03_coinft_teensy/` 下的 CoinFT 逆向档案和 `00_CORE/06_our_build/` 阶段构建。归档中的伪代码是对二进制行为的分析记录，不等同于厂商原始源码。Stage 43/44 的 HEX 可编程到目标硬件，但烧录会改变设备 Flash；烧录前应阅读各阶段说明并确认板卡型号、备份和恢复流程。

阶段源码的 Makefile 仍记录原研究工作区中的依赖路径。为了控制仓库大小，本归档没有复制 ModusToolbox、CapSense、Em_EEPROM、PDL 和共享 BSP 依赖，因此在此目录直接运行 `make verify` 会因缺少外部依赖而停止；已保存的 ELF/HEX 是原研究工作区中生成的正式输出。`reconstruction_v13` 同样是主机侧验证材料，不是可烧录工程。

重建候选 v13 仍有外设、时钟、引脚切换及中断时序等未闭合事项，不应烧录。历史 MTB 输出的可复现性受未随档案复制的 ModusToolbox 依赖影响。不要仅凭文件名选择镜像，按上表的类型和各自 README 判断。

## 校验

在仓库根目录运行：

```sh
shasum -a 256 firmware_reverse_engineering/original_firmware/CoinFT_V2_firmware.hex
```

原始 HEX 的预期 SHA-256 已列在上表。
