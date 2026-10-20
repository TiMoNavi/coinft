# 新电脑部署与快捷命令

本说明用于在新电脑上运行 Teensy 端 MLP 的 CoinFT 工作区。所有命令都从工作区目录运行，或通过 `COINFT_WS` 指向工作区，因此不依赖终端当前所在目录。

## Linux 快速启动波形图

下面按顺序在装有桌面环境的 Ubuntu/Linux 电脑执行。Teensy 用 USB 连到这台电脑；左、右 CoinFT 仍接在 Teensy 上。波形窗口每边显示一张图，每张图叠加六轴曲线。

先安装 Git、Git LFS 和 Python 图形依赖。跳过 Git LFS 文件下载，因为波形图和 Teensy 输出接口不需要模型压缩包或训练视频：

```bash
sudo apt update
sudo apt install -y git git-lfs python3 python3-venv python3-tk
git lfs install
GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 https://github.com/TiMoNavi/coinft.git
cd coinft
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install pyserial matplotlib
```

连接 Teensy 后，运行下面整段。它会自动查找 Teensy 4 USB 串口（USB ID `16c0:0483`），找到后直接打开左右手波形图：

```bash
PORT="$(.venv/bin/python -c 'from serial.tools import list_ports; print(next((p.device for p in list_ports.comports() if p.vid == 0x16C0 and p.pid == 0x0483), ""))')"
if [ -n "$PORT" ]; then
  echo "Teensy 串口：$PORT"
  .venv/bin/python teensy_mlp_workspace/visualization/plot_dual_wrench.py --port "$PORT"
else
  echo "没有自动找到 Teensy。可用串口如下："
  .venv/bin/python -m serial.tools.list_ports -v
  echo "从上面找到 Teensy 的 /dev/ttyACM* 端口后，替换下面命令中的端口再运行："
  echo '.venv/bin/python teensy_mlp_workspace/visualization/plot_dual_wrench.py --port /dev/ttyACM0'
fi
```

如果显示 `PermissionError` / `Permission denied`，给当前用户串口权限，然后注销并重新登录 Linux：

```bash
sudo usermod -aG dialout "$USER"
```

运行波形图需要 Linux 图形桌面；纯 SSH 终端没有显示环境时不会弹出窗口。波形程序启动传感器并重新执行 Teensy 端归零，开始时请保持两侧传感器空载，等待状态变为 `ready` 后再施力。关闭窗口会释放串口。

如果不想自动查找端口，也可直接指定常见的 `/dev/ttyACM0`：

```bash
.venv/bin/python teensy_mlp_workspace/visualization/plot_dual_wrench.py --port /dev/ttyACM0
```

## 1. 准备工作区

把整个 `teensy_mlp_workspace` 文件夹复制到新电脑的任意位置。例如：

```text
~/Projects/teensy_mlp_workspace
```

工作区应包含 `firmware`、`host`、`visualization`、`tests`、`tools` 和 `platformio.ini`。原始 `coin-ft` 项目无需修改。模型参数已经生成在固件的 `model_params.h` 中；如果要重新生成模型参数，则还需要取得原训练模型文件。

## 2. 安装工具

需要 Python 3、pip、Git（可选）和 Teensy 支持的 PlatformIO。建议为 Python 依赖创建虚拟环境：

```bash
cd ~/Projects/teensy_mlp_workspace
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install pyserial matplotlib
python -m pip install platformio
```

PlatformIO 首次编译时会自动下载 Teensy 平台和工具链。macOS 上如果系统未安装命令行编译工具，请先运行 `xcode-select --install`。Windows 和 Linux 可按 PlatformIO 与 Teensy 的官方环境要求安装相应 USB 驱动或权限规则。

## 3. 查找 Teensy 串口

连接 Teensy 后查询串口：

```bash
python -m serial.tools.list_ports -v
```

macOS 通常显示为 `/dev/cu.usbmodem...`，Linux 通常为 `/dev/ttyACM0`，Windows 通常为 `COM3` 等。下文示例端口需要替换成实际端口。

固件目标是 Teensy 4.0 或 Teensy 4.1。编译但不烧录：

```bash
pio run -e teensy40
pio run -e teensy41
```

烧录对应目标：

```bash
pio run -e teensy40 -t upload
pio run -e teensy41 -t upload
```

如果 Teensy Loader 等待设备，请按一下板上的 Program 按钮。烧录前先关闭正在读取 Teensy USB 串口的接收器或波形窗口。

## 4. 在任意目录运行

### 每次显式指定工作区

在任意目录运行时，使用工作区脚本的绝对路径：

```bash
python3 "$HOME/Projects/teensy_mlp_workspace/visualization/plot_dual_wrench.py" --port /dev/cu.usbmodem123
```

Python 导入标准接口时，把工作区目录加入 `PYTHONPATH`：

```bash
PYTHONPATH="$HOME/Projects/teensy_mlp_workspace" python3 -c 'from host.coinft_interface import CoinFTInterface; print("接口导入成功")'
```

### 设置环境变量和快捷函数（zsh/bash）

把以下内容加到 `~/.zshrc` 或 `~/.bashrc`，并按本机位置修改 `COINFT_WS`。激活虚拟环境后，`COINFT_PY` 会优先使用该环境中的 Python。

```bash
export COINFT_WS="$HOME/Projects/teensy_mlp_workspace"
export COINFT_PORT="/dev/cu.usbmodem123"
export COINFT_PY="$COINFT_WS/.venv/bin/python"
export COINFT_PIO="$COINFT_WS/.venv/bin/pio"

coinft-dual() {
  "$COINFT_PY" "$COINFT_WS/visualization/plot_dual_wrench.py" --port "$COINFT_PORT" "$@"
}

coinft-axes() {
  "$COINFT_PY" "$COINFT_WS/visualization/plot_axis_compare.py" --port "$COINFT_PORT" "$@"
}

coinft-read() {
  "$COINFT_PY" "$COINFT_WS/host/receiver.py" --port "$COINFT_PORT" "$@"
}

coinft-build() {
  "$COINFT_PIO" run --project-dir "$COINFT_WS" -e teensy40 "$@"
}
```

重新打开终端，或执行 `source ~/.zshrc` / `source ~/.bashrc`。之后可在任意目录使用：

```bash
coinft-dual                 # 左右手各一张波形图，每张叠加六轴曲线
coinft-axes                 # 六个轴：左右手曲线叠加比较
coinft-read                 # 终端接收 JSON Lines
coinft-read --output run.jsonl
coinft-build                # 编译 Teensy 4.0
```

波形窗口左下角的“重新归零”会发送 `i`、`s`，让 Teensy 停止并重新启动两侧传感器流，同时重跑板端稳定性、tare、bias、滤波和零点检查。稳定性检查通过时约需 14 秒，操作时保持传感器空载。

在命令中临时覆盖端口：

```bash
COINFT_PORT=/dev/ttyACM0 coinft-dual
```

没有虚拟环境时，将 `COINFT_PY` 和 `COINFT_PIO` 分别设为系统的 `python3` 和 `pio` 可执行文件路径。

## 5. 用 Python 调用六维力

标准输出顺序为 `[Fx, Fy, Fz, Mx, My, Mz]`，力单位为 N，力矩单位为 Nm。左手使用模型 `0001` 和 `Serial5`，右手使用模型 `0010` 和 `Serial1`。电脑通过 Teensy USB 串口同时读取两侧结果：

```python
from host.coinft_interface import CoinFTInterface

with CoinFTInterface("/dev/cu.usbmodem123") as device:
    while True:
        sample = device.read_sample()
        if sample.wrench_valid:
            left_fx = sample.left_wrench[0]
            right_fz = sample.right_wrench[2]
            print(sample.calibration_status, left_fx, right_fz)
```

这段程序需要从工作区目录运行，或先设置 `PYTHONPATH="$COINFT_WS"`。完整的独立调用示例：

```bash
"$COINFT_PY" "$COINFT_WS/host/read_wrench.py" --port "$COINFT_PORT"
```

默认启动会向 Teensy 发送 `i`、`s`，触发传感器流和板端自动校准。等待 `ready` 后再使用有效力数据。退出时会发送 `i` 停止传感器流。需要接入已有流且不重启校准时，可给波形程序添加 `--no-start-command`，或在接口中使用 `CoinFTInterface(port, start_stream=False)`。

## 6. 串口使用和验证

波形程序自己从 Teensy USB 串口读数据，不需要同时打开单独的接收器。串口同一时间只能由一个程序打开：要从终端接收器切换到波形图，先用 `Ctrl+C` 退出接收器，再运行 `coinft-dual` 或 `coinft-axes`。波形窗口关闭时会释放串口。

运行测试：

```bash
"$COINFT_PY" -m unittest discover -s "$COINFT_WS/tests" -v
```

Teensy 4.0 编译验证：

```bash
"$COINFT_PIO" run --project-dir "$COINFT_WS" -e teensy40
```

详细任务范围、协议和已完成的设备验证记录见 [README.md](README.md) 与 [TASK.md](TASK.md)。
