# Teensy CoinFT MLP Workspace

This workspace keeps the Teensy inference migration separate from the existing
CoinFT source tree. The firmware reads the right CoinFT on `Serial1` and the
left CoinFT on `Serial5`, runs their respective embedded MLPs, and sends
left/right six-axis results over the Teensy's USB serial connection. The host
receiver decodes those results as JSON Lines.

The exact task scope and hardware/model mapping are in [TASK.md](TASK.md).
For new-computer setup, relocatable paths, and shell shortcuts, see
[DEPLOYMENT.md](DEPLOYMENT.md).
Linux users can follow the copy-paste [Linux waveform quick start](DEPLOYMENT.md)
to clone the repository, install plotting dependencies, find the Teensy USB
port, and launch both six-axis plots.

面向其他采集程序的中文接入说明见下方[“其他采集程序接入 CoinFT”](#其他采集程序接入-coinft)。

## 其他采集程序接入 CoinFT

这一节描述的是当前 Teensy MLP 固件的实际接口。接入程序不需要在电脑上
安装 ONNX Runtime，也不需要再次加载 `models (3) (1)` 中的模型；MLP、归一化、
反归一化和当前的校准/滤波都已经在 Teensy 内完成。电脑只需要打开 Teensy 的
USB 串口、校验 `CFTF v2` 帧，并读取有效的左右手六维结果。

### 数据流和设备对应关系

```text
左 CoinFT --TX5/RX5--> Teensy Serial5 --┐
                                        ├--> Teensy USB 串口 --> 电脑采集程序
右 CoinFT --TX1/RX1--> Teensy Serial1 --┘
```

| 侧别 | Teensy 接口 | 固件使用的模型 | 输出槽位 |
| --- | --- | --- | --- |
| 左手 | `Serial5`，TX5/RX5 | `0001` | `left` |
| 右手 | `Serial1`，TX1/RX1 | `0010` | `right` |

左右模型和归一化参数不能互换。固件按照训练时的 12 路通道顺序把原始值送入
对应模型；采集程序不要重新排列、缩放或交换六个输出轴。

### 采样率、链路和延时

| 项目 | 当前实现和实测依据 |
| --- | --- |
| CoinFT 原始帧率 | 仓库既有逻辑分析记录约 `200–202 Hz`；原始帧率由 CoinFT 传感器链路决定。当前 USB 记录没有分别计数左右 UART 原始帧。 |
| 双侧配对输出率 | 本次电脑端实测约 `200.5 Hz`（9,468 个 CFTF 帧 / 47.2 秒），帧间隔约 `4.99 ms`。这是配对后 Teensy 输出的实测值，不是所有设备的硬保证；左右 CoinFT 原始 UART 帧率未在这次 USB 记录中分别计数。 |
| CoinFT 到 Teensy | 每侧 UART `1,000,000 baud`，输入帧为 26 字节；帧内没有硬件时间戳。 |
| Teensy 配对 | 左右两帧的到达时间差超过 `10 ms` 时不配对，等待下一组新帧。这个是配对条件，不等于每个样本都会增加 10 ms 延迟。 |
| Teensy 处理上限 | 固件输出循环设置了不小于 `1 ms` 的输出间隔，即处理上限约 1 kHz；它不是当前设备的实际输出率。 |
| MLP 推理耗时 | 当前记录没有单独测量一次 MLP 前向计算的 CPU 耗时，采集程序不要写死一个未经测量的“MLP 延迟”。 |
| 五点移动平均 | 固件保留最近 5 个样本做因果平均。在本次约 200.5 Hz 的输出率下，滤波器群延迟约为 2 个样本，即约 `10 ms`；这是算法滤波延迟，不是 MLP 指令执行时间。 |
| Teensy 到电脑 | 使用有线 USB CDC 串口。代码中的 `Serial.begin(115200)` 是串口接口配置；USB 端到端传输耗时当前没有单独测量，不能把 115200 直接当作 USB 的物理吞吐上限。 |

因此，目前能明确量化的是五点滤波器本身约 10 ms 的群延迟；它不代表完整的
Teensy 处理或端到端延迟。传感器到电脑的总延迟还包含采样相位、左右帧配对、MLP
执行和 USB 调度。
当前协议只能提供 Teensy 配对处理时刻 `teensy_time_us` 和电脑收到时刻
`host_receive_monotonic_ns`；由于 CoinFT 原始帧没有传感器硬件时间戳，尚未形成完整的
端到端延迟实测值。

### CoinFT 发给 Teensy 的原始帧

每个 CoinFT 输入帧固定 26 字节，按下面的顺序发送：

```text
byte 0       0x02                         帧开始
byte 1..24   12 × uint16 little-endian   原始通道 0..11
byte 25      0x03                         帧结束
```

Teensy 只在收到完整的开始/负载/结束帧后更新该侧缓存。12 路原始值是模型输入，
不是六维力；如果其他程序直接接在 CoinFT UART 上，必须保留 little-endian 顺序，
不能把它误当成 `CFTF` 六轴输出帧。通常的采集程序应接 Teensy 的 USB 口，而不是
另开一个程序抢占两条 CoinFT UART。

### Teensy 发给电脑的六轴帧

输出是固定 65 字节的二进制 `CFTF v2` 帧。所有整数和浮点数均为 little-endian，
浮点数是 IEEE-754 `float32`：

这是 Teensy MLP 工作区的新六轴输出协议。原项目中用于原始通道或旧桥接协议的
接收器不能直接把它当作六轴帧解析，必须按下面的 version、长度和 CRC 规则解码。
第三方解码器需允许 `flags` 的 bit 0 和 bit 1；bit 1 是非致命归零提示，不能据此丢弃
六轴载荷。

| 字节偏移 | 长度 | 字段 | 含义 |
| ---: | ---: | --- | --- |
| `0..3` | 4 | `magic` | ASCII `CFTF` |
| `4` | 1 | `version` | `2` |
| `5` | 1 | `calibration_status` | 校准状态编号，见下表 |
| `6` | 1 | `flags` | bit 0 表示六轴有效；bit 1 表示零点检查有扰动警告 |
| `7..10` | 4 | `sequence` | uint32 序号，用于检测丢帧 |
| `11..14` | 4 | `teensy_time_us` | Teensy `micros()` 时间戳，uint32 |
| `15..38` | 24 | `left` | 6 × float32，顺序 `Fx,Fy,Fz,Mx,My,Mz` |
| `39..62` | 24 | `right` | 6 × float32，顺序 `Fx,Fy,Fz,Mx,My,Mz` |
| `63..64` | 2 | `crc16` | CRC-16/CCITT-FALSE，小端序 |

如果使用 Python 原生解析，固定帧的整体布局对应
`struct.Struct("<4sBBBII12fH")`；C/C++ 接入时不要直接依赖带自然对齐的结构体，
应按上表的字节偏移读取，或显式使用 packed 结构体。

CRC 的覆盖范围是字节 `4..62`（包含 version 到 right 载荷），参数为：
多项式 `0x1021`、初值 `0xFFFF`、不反射、最终异或 `0x0000`。解析时应按顺序检查
magic、版本、完整长度、CRC、有限浮点数，再使用载荷。

校准状态编号如下。只有 `calibration_status == 7` 且 `flags & 0x01 != 0` 时，
六维力才可以交给下游程序；其他状态的六个浮点数只是无效占位值。`flags & 0x02`
表示零点检查窗口受到扰动或检查均值超过 `0.15 N`。它是警告，不会清除 bit 0，六轴
数据仍有效并持续发送。

| 编号 | 状态 | 采集程序行为 |
| ---: | --- | --- |
| 0 | `warming_up` | 等待，不使用力值 |
| 1 | `delayed` | 原始值还不稳定，等待 |
| 2 | `provisional_tare` | 临时归零，等待 |
| 3 | `runtime_settling` | 运行稳定阶段，等待 |
| 4 | `final_tare` | 最终归零阶段，等待 |
| 5 | `bias_calibration` | 力偏差校准阶段，等待 |
| 6 | `zero_check` | 无载零点检查，等待 |
| 7 | `ready` | 可以使用六维值 |
| 8 | `zero_check_failed` | 旧固件兼容状态；新固件不会因零点偏差停止输出 |
| 9 | `error` | 推理或校准错误，停止使用 |

### 每次采集开始时归零

每次开始一段新的采集，都必须让 Teensy 执行一次板端归零。标准 Python 接口、
`receiver.py` 和两个波形图默认都会发送 `i`、`s`：`i` 停止两只 CoinFT，`s`
重新启动传感器并触发 Teensy 校准。不要在新采集开始时使用
`--no-start-command` 或 `CoinFTInterface(..., start_stream=False)`；这两个选项只用于
接入已经运行、已经校准的流。`t` 命令只转发给 CoinFT，不会触发 Teensy 归零。

Teensy 每次上电后第一次收到 `s`，至少等待 20 秒再开始原始数据稳定性判断，之后还要
完成 tare、力偏差校准和零点检查。数据稳定时，通常约 28–30 秒到达 `ready`；若数据
不稳定，等待时间会更长。首次启动时保持两侧空载、静止，等状态为 `ready` 后再开始
施力或采集。

同一次上电期间后续每次 `s` 都走快速归零：Teensy 采集约 1 秒原始值均值作为新 tare，
重新计算约 1 秒力偏差，再做 1 秒零点检查，通常约 3 秒到达 `ready`，不再等待首次
20–30 秒稳定期。快速归零不代表可以带载归零：这约 3 秒内仍须保持传感器空载且静止，
否则当前受力可能被当成零点。`ready` 表示该次归零完成；只有 `ready` 帧中的六轴值
有效。零点检查失败时不要继续使用该次采集数据。

每次成功解析一帧后，用 `sequence` 与上一帧比较；中间跳过的序号应记录为丢帧。
`teensy_time_us` 会按 uint32 回绕，不能当作永久递增的 wall-clock 时间。

### 六维力的定义

六维结果固定为：

```text
[Fx, Fy, Fz, Mx, My, Mz]
```

`Fx/Fy/Fz` 是 CoinFT 模型坐标系 X/Y/Z 方向的力，单位为牛顿（`N`）；
`Mx/My/Mz` 是绕 X/Y/Z 轴的力矩，单位为牛顿米（`Nm`）。这六项的顺序、单位和
模型符号与 CoinFT 训练目标一致。左右手只影响模型和结果槽位，不会导致轴交换或改符号。
当前工作区已经验证了模型数值和空载归零流程；物理轴正方向、比例和已知负载响应仍应
用标准力/力矩做最终硬件验证。

### 推荐的接入方式

最简单的方式是复用工作区的标准 Python 接口。它会处理缓存、magic、CRC、序号丢失
和校准状态：

```python
from host.coinft_interface import CoinFTInterface

with CoinFTInterface("/dev/ttyACM0") as coinft:
    first = coinft.wait_until_ready(timeout=40)  # 首次上电通常约 28–30 秒
    print(first.left_wrench, first.right_wrench)
    while True:
        sample = coinft.read_sample()  # 阻塞到一帧完整的 CFTF v2
        if not sample.wrench_valid:
            continue
        left_fx = sample.left_wrench[0]
        right_fz = sample.right_wrench[2]
        print(sample.sequence, left_fx, right_fz)
```

创建 `CoinFTInterface` 时会自动为这一段采集发送启动和归零命令。如果同一个程序要
连续采集多段数据，每段开始前调用 `coinft.restart_calibration()`；不要只在进程启动时
归零一次后就把后续数据段直接拼接起来。

`wait_until_ready(timeout=...)` 会忽略校准阶段帧，收到有效 `ready` 帧后返回。零点检查
有扰动时，样本的 `zero_check_warning` 为 `True`，`sample.as_dict()` 也提供 `warning`
字段；该状态只提示，不会停止采集。固件推理错误会抛出 `CalibrationFailedError`，等待
超时会抛出 `TimeoutError`。外部程序可以记录提示后继续处理：

```python
from host.coinft_interface import CoinFTInterface

with CoinFTInterface("/dev/ttyACM0") as coinft:
    sample = coinft.wait_until_ready(timeout=40)
    while True:
        if sample.zero_check_warning:
            print("CoinFT zero-check warning; continuing with valid wrench data")
        process(sample.left_wrench, sample.right_wrench)
        sample = coinft.read_sample()
```

零点检查超过阈值不会卡住长时间采集，也不会自动重启或重复 tare。警告只描述
`zero_check` 窗口的情况；如果 tare/bias 期间受到持续外力，归零基线也可能偏移，因此开始
新采集时仍建议尽量保持空载。警告阈值不代表几牛顿的外力会被抵消或仍具有测量准确性。

从任意目录运行时，把工作区加入 `PYTHONPATH`，或者直接使用 JSONL 入口：

```bash
export COINFT_WS="$HOME/Projects/teensy_mlp_workspace"
export PYTHONPATH="$COINFT_WS"
python3 "$COINFT_WS/host/read_wrench.py" --port /dev/ttyACM0
```

`read_wrench.py` 每行输出一个 JSON 对象，包含 `left`、`right`、
`wrench_axes`、`sequence_id`、`teensy_time_us`、`calibration_status` 和
`wrench_valid`。需要写文件时使用：

```bash
python3 "$COINFT_WS/host/receiver.py" \
  --port /dev/ttyACM0 \
  --output "$COINFT_WS/run.jsonl"
```

其他语言可以直接按上面的 65 字节布局实现解析，或把
[`host/protocol.py`](host/protocol.py) 作为 CRC、帧重同步和字段映射的参考。
电脑端不应同时启动 `receiver.py`、波形图和另一个串口程序；一个 USB 串口同一时间
只能由一个进程打开。默认的 `CoinFTInterface` 会发送 `i` 再发送 `s`，停止并重启
传感器流、重新执行板端校准；如果要接入已经运行的流，使用
`CoinFTInterface(port, start_stream=False)`。

## Files

- `firmware/teensy_coinft_mlp/teensy_coinft_mlp.ino`: dual sensor UART parsing and USB output.
- `firmware/teensy_coinft_mlp/inference.cpp`: float32 dense layers, ReLU, and input/output normalization.
- `firmware/teensy_coinft_mlp/calibration.cpp`: runtime raw stability, provisional/final tare, force bias, zero check, and five-sample moving average.
- `firmware/teensy_coinft_mlp/model_params.h`: generated weights and normalization values for model `0001` (left) and `0010` (right).
- `firmware/teensy_coinft_mlp/output_protocol.cpp`: fixed-size frame encoder and CRC.
- `host/protocol.py`: streaming frame decoder and JSONL field mapping.
- `host/coinft_interface.py`: reusable `CoinFTInterface` serial API and `CoinFTSample` values.
- `host/receiver.py`: serial receiver; sends `i`, then `s`, and sends `i` on exit.
- `host/read_wrench.py`: minimal standalone JSON Lines example using the standard API.
- `visualization/plot_dual_wrench.py`: two training-style waveform plots, one per hand, with all six axes overlaid in each plot.
- `visualization/plot_axis_compare.py`: six axis plots with left and right traces overlaid.
- `tools/generate_model_header.py`: extracts the ONNX initializers and normalization JSON.
- `tools/verify_models.py`: compiles the portable C++ core and compares both models with ONNX Runtime.

The Teensy sketch is in a same-named directory and can be opened as
`firmware/teensy_coinft_mlp/teensy_coinft_mlp.ino` in Arduino IDE with the
Teensy board package installed. PlatformIO Teensy 5.1.0 has built both Teensy
4 targets and uploaded the `teensy40` build to the connected Teensy 4.0 using
the Teensy Loader GUI protocol.

The firmware pairs the most recently received frame from each sensor only when
both frames arrived within 10 ms. The CoinFT sensor frames have no timestamp in
the observed protocol, so this is a bounded arrival-time pairing rather than a
hardware-synchronized sample guarantee.

Paired samples are processed at no more than 1 kHz. The calibration ring holds
4096 samples (about 4.096 seconds at that rate) for the existing four-second
stability comparison. Its raw history uses about 208 KiB of static RAM, so the
actual board target's RAM report should be checked. PlatformIO builds for
Teensy 4.0 and 4.1 both succeeded with platform 5.1.0; each reported 327296
bytes in RAM1 and about 164 KiB left for local variables.

## Output Frame

Each output frame is 65 bytes:

```text
0..3   ASCII "CFTF"
4      version = 2
5      calibration status: 0 warming_up, 1 delayed, 2 provisional_tare,
       3 runtime_settling, 4 final_tare, 5 bias_calibration, 6 zero_check,
       7 ready, 8 legacy zero_check_failed, 9 error
6      flags: bit 0 means wrench values are valid; bit 1 means zero-check warning
7..10  sequence, uint32 little-endian
11..14 Teensy micros(), uint32 little-endian
15..38 left Fx,Fy,Fz,Mx,My,Mz, six IEEE-754 float32 little-endian values
39..62 right Fx,Fy,Fz,Mx,My,Mz, six IEEE-754 float32 little-endian values
63..64 CRC-16/CCITT-FALSE, uint16 little-endian
```

CRC parameters are polynomial `0x1021`, initial value `0xFFFF`, no reflection,
and final xor `0x0000`; coverage is bytes 4 through 62 inclusive. Force values
are in N and moment values in Nm. During calibration, the values are zero
placeholders and the validity bit is clear. The host emits null wrench fields
until the `ready` state. Ready frames contain the tare, force-bias, and
five-sample filtered values. `provisional_tare` is a transient internal state;
the implementation advances to `runtime_settling` in the same sample call.

## Host Receiver

Install the host-side dependencies if they are not already available:

```bash
python3 -m pip install pyserial
```

Receive and print JSONL to stdout:

```bash
python3 teensy_mlp_workspace/host/receiver.py --port /dev/cu.usbmodem164546801
```

Write the JSONL stream to a file:

```bash
python3 teensy_mlp_workspace/host/receiver.py \
  --port /dev/cu.usbmodem164546801 \
  --output teensy_mlp_workspace/run.jsonl
```

The receiver starts and stops sensor streaming with the existing `s` and `i`
commands. It emits calibration state, `left_wrench`, `right_wrench`, and named
axis fields in the CoinFT order `fx, fy, fz, mx, my, mz`.

## Live Waveforms

The workspace includes two alternatives. The plot program itself reads the
Teensy USB serial stream; a separate receiver process is not required. Only
one program can own the Teensy USB serial port at a time. Both decode the
existing 65-byte CFTF v2 frames and show calibrated Teensy outputs without
loading ONNX models or recalibrating on the computer:

```bash
python3 teensy_mlp_workspace/visualization/plot_dual_wrench.py \
  --port /dev/cu.usbmodem164546801
```

This view shows one plot per hand, with its six axes overlaid. Force axes use
the left N scale and moment axes use the right Nm scale; red, green, and blue
identify X, Y, and Z, with all six traces drawn as thin solid lines. It keeps
a five-second history and updates every 40 frames, following the CoinFT
visualizer's batch refresh behavior. The alternate view puts each axis in a
separate panel for direct left/right comparison:

The lower-left `重新归零` button sends `i` then `s` to the Teensy. The first
calibration after board power-up waits at least 20 seconds for raw stability
before tare, bias calibration, and zero check; it usually reaches `ready` in
about 28-30 seconds when the input is stable. Later starts on the same power
cycle use a one-second raw tare, one-second bias calibration, and one-second
zero check, usually reaching `ready` in about three seconds. Keep both sensors
unloaded and still during either procedure.

```bash
python3 teensy_mlp_workspace/visualization/plot_axis_compare.py \
  --port /dev/cu.usbmodem164546801
```

Both start streaming with `i` then `s`, so the Teensy runs the full first-start
calibration after power-up and the short tare on later starts. Wait for `ready`
before reading plotted values. Closing the plot or pressing Ctrl+C sends `i`
and closes the serial port. To attach to an already running, already calibrated
stream without starting a new acquisition, add `--no-start-command`.

## Standard Python Interface

For code that needs wrench samples instead of a plot, use the reusable API:

```python
from host.coinft_interface import CoinFTInterface

with CoinFTInterface("/dev/cu.usbmodem164546801") as coinft:
    while True:
        sample = coinft.read_sample()
        if sample.wrench_valid:
            left_fx = sample.left_wrench[0]
            right_fz = sample.right_wrench[2]
            print(sample.calibration_status, left_fx, right_fz)
```

The minimal standalone caller prints the full sample as JSON Lines:

```bash
python3 teensy_mlp_workspace/host/read_wrench.py \
  --port /dev/cu.usbmodem164546801
```

Use `sample.as_dict()` for named `left`/`right` axis values, sequence and
timestamp metadata, calibration state, and dropped-frame count. `read_sample()`
blocks for one CRC-checked frame. By default the interface sends `i`/`s` on
startup and `i` on context exit. Pass `start_stream=False` to attach without
sending sensor commands.

## Device Test

On 2026-10-20, `/dev/cu.usbmodem164546801` was identified as Teensy 4.0 and
successfully programmed with the `teensy40` build. A 47.2-second host capture
decoded 9,468 valid frames at about 200.5 Hz with no sequence gaps. Calibration
reached `ready` after the raw stability, tare, bias, and zero-check stages; the
1-second force zero check passed its 0.15 N per-axis mean threshold on both
sides. During 6,857 ready frames, mean left wrench was
`[0.000201, 0.000357, 0.011694, -0.000063, 0.000134, -0.000039]` and mean right
wrench was `[0.010879, -0.003592, -0.080019, -0.000201, 0.000338, -0.000128]`
in `[N, N, N, Nm, Nm, Nm]`. No known force or moment was applied, so physical
axis sign, scale, and loaded response remain unverified. The detailed record
is in [TASK.md](TASK.md).

On 2026-10-21, the updated calibration firmware was programmed to the same
Teensy 4.0 and tested through `CoinFTInterface`. The first calibration after
startup reached `ready` in 28.02 seconds. Without powering off the board,
`restart_calibration()` reached `ready` again in 3.01 seconds. Both sides sent
valid six-axis wrench samples; the parser reported zero bad frames and zero
discarded bytes. No known force or moment was applied during this check, so
this confirms the zeroing flow and live output path, but does not validate
loaded response, axis sign, or scale. The detailed record is in [TASK.md](TASK.md).

The non-blocking zero-check warning update was also programmed on 2026-10-21.
After its full startup calibration, the board reached `ready` and delivered
1,004 consecutive valid wrench samples with no parser errors or discarded
bytes. This quiet run did not trigger a warning. The offline disturbance test
confirmed that an over-threshold zero-check still reports `ready` with valid
wrenches and sets the warning flag.

## Regeneration and Verification

Regenerate the header after replacing or updating either source model:

```bash
python3 -m pip install numpy onnx
python3 teensy_mlp_workspace/tools/generate_model_header.py
```

Run host protocol tests and check C++ inference against both ONNX models:

```bash
python3 -m pip install numpy onnxruntime
python3 -m unittest discover -s teensy_mlp_workspace/tests -v
python3 teensy_mlp_workspace/tools/verify_models.py --samples 512
```

The offline verifier needs a C++ compiler (`g++` or `clang++`). It checks both
MLPs against ONNX Runtime, protocol bytes against the Python encoder, and all
observable calibration states and outputs against the existing Python
`CoinFTWarmup`. The Teensy 4.0 build and live USB output have been verified;
known-load response and physical axis validation still need hardware testing.

Build both available Teensy 4 targets without uploading:

```bash
"$HOME/Library/Python/3.9/bin/platformio" run \
  --project-dir teensy_mlp_workspace -e teensy40 -e teensy41
```
