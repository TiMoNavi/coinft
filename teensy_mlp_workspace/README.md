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
       7 ready, 8 zero_check_failed, 9 error
6      flags: bit 0 means wrench values are valid
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

The lower-left `重新归零` button sends `i` then `s` to the Teensy. This stops
both sensor streams, resets the Teensy calibration state, and starts the full
stability, tare, bias, filter, and zero-check sequence again. Keep both sensors
unloaded while it runs; the full sequence takes about 14 seconds when the raw
stability check passes.

```bash
python3 teensy_mlp_workspace/visualization/plot_axis_compare.py \
  --port /dev/cu.usbmodem164546801
```

Both start streaming with `i` then `s`, so the Teensy repeats its automatic
stability, tare, bias, and zero-check sequence. Wait for `ready` before reading
the plotted values. Closing the plot or pressing Ctrl+C sends `i` and closes
the serial port. To attach without restarting the current calibration, add
`--no-start-command`.

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
