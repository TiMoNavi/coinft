# Stage 44: integer MLP experiment

This sibling image is an experiment for running the 0019 CoinFT MLP on the
same CY8C4149AZI-S593 that runs the Stage 43 CapSense/UART maintenance code.
Stage 43 is unchanged.

The network is `12 -> 128 -> 64 -> 36 -> 24 -> 12 -> 6`. The generated model
uses symmetric per-output-channel int8 weights, signed int16 activations,
int32 accumulators, and integer requantization. Two 128-element activation
buffers are reused; weights and quantization constants remain in Flash. No
dynamic allocation or floating-point arithmetic is used by the MLP path.

The model is generated from the checked-in ONNX, external-data, and norm files:

```sh
make clean verify
```

This regenerates `src/mlp_model_data.c`, `tests/golden_vectors.*`, and
`quantization_manifest.json`, runs Python and native-C golden-vector checks,
then builds the ARM image and checks its linker placement. The `.mlp_model`
section starts at `0x5200`, after the fixed Em_EEPROM region `0x5000..0x51ff`.
The startup `.data` load image is also placed after the model in Flash.

## Experimental protocol

The legacy commands and raw stream remain unchanged:

| Command | Behavior |
| --- | --- |
| `q` | returns `0x1a` |
| `s` / `i` | starts/stops the existing 26-byte raw stream |
| `t`, `w`, `n` | same Stage 43 maintenance behavior |
| `z` | scans a complete pair, records the current raw counts as a temporary tare, replies `Z` |
| `p` | scans a complete pair, runs MLP, returns one 114-byte diagnostic frame |
| `r` | returns the most recent diagnostic frame without scanning |

Inference is performed in the main loop after a complete two-phase scan pair,
never from an interrupt. During `s`, this is approximately 100 inferences/s
while the existing timer ISR continues to send raw frames at approximately
200 Hz.

Diagnostic frame layout is little-endian:

```text
A5 5A version length
sequence uint32
raw[12] uint16
zeroed[12] int32
output_micro[6] int32   # uN, uN, uN, uNm, uNm, uNm
inference_cycles uint32 # 48 MHz core cycles
saturation_count uint16
flags uint16             # bit 0: tare_valid
crc16-CCITT uint16       # over all preceding bytes
```

The temporary tare is for repeatable firmware testing only. This image does
not implement the host calibration contract's stability detector, per-device
model selection, axis/sign mapping, force bias subtraction, or five-sample
moving average. Its six output values therefore characterize the embedded
integer MLP, not production-calibrated wrench output.

## Hardware evidence

The protected run on KitProg3 / `/dev/cu.usbmodem14403` is recorded in
`verification/live/runs/20260905-095625_stage44_integer_mlp/`.

Results:

- Stage 43 full Flash was read back before programming (393,216 bytes) and
  matched the Stage 43 image outside the expected Em_EEPROM runtime window.
- Stage 44 mass erase, programming, and OpenOCD verify succeeded.
- First run: 200 raw frames at 200.521 Hz; 102 MLP updates during the stream;
  17 diagnostic frames matched the Python integer reference.
- Second run: 400 raw frames at 200.598 Hz; 204 MLP updates; 25 diagnostics
  matched.
- After SWD reset: 100 raw frames at 200.546 Hz; 51 MLP updates; 13 diagnostics
  matched.
- Across all 42 diagnostics, activation saturation was zero. Inference took
  245,147 to 250,297 core cycles, or about 5.11 to 5.21 ms at 48 MHz.
- The Stage 44 Flash readback matched its image outside the Em_EEPROM window.

After testing, the exact pre-test 393,216-byte readback was restored and a
post-restore readback was verified. The board is left running the known-good
Stage 43 image.

## Offline visual check

`tools/visualize_stage44.py` reuses the processing approach in
`/Users/550m/code/coin-ft/visualization/visualize_coinfts.py` (ONNX Runtime
plus Matplotlib) while enforcing the exact 0019 model. For the recorded run:

```sh
python3 tools/visualize_stage44.py \
  --evidence ../verification/live/runs/20260905-095625_stage44_integer_mlp/live_first.json \
             ../verification/live/runs/20260905-095625_stage44_integer_mlp/live_second.json \
             ../verification/live/runs/20260905-095625_stage44_integer_mlp/live_after_reset.json \
  --output ../verification/live/runs/20260905-095625_stage44_integer_mlp/stage44_target_vs_onnx.png
```

It creates target-vs-ONNX, error, and performance PNGs plus a JSON summary.
The original repository's `calibration/coinft_model_check.py` was also run on
the final Stage 43 raw capture with the same 0019 model; its finite-output
report is `stage43_onnx_model_check.json` beside those plots.
