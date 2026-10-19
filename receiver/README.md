# Revised CoinFT receiver

`standard_receiver.py` is the host-side receiver for the revised Teensy
stream. It keeps the physical sensor order and calibration behavior explicit:

- dual mode reads `00 00 + sequence_id + teensy_time_us + left[12] + right[12]`;
- single mode can read `single-v2`, `single-framed24`, or the current firmware's
  bare `single-legacy24` payload;
- left and right each load their own ONNX and normalization files;
- startup calibration waits for raw stability, performs provisional and final
  raw tares, estimates a fixed model force bias, and checks the unloaded zero;
- JSONL and CSV output contain raw channels, timestamps, calibration status, and
  calibrated six-axis force/moment values;
- a `.meta.json` sidecar records the selected models, sequence loss, offsets, and
  final model bias;
- dual mode can display live force and moment plots with `--plot`.

The default model file is `configs/coinft_models_0019_0012.json`, which points
to the production `0019` left model and `0012` right model. A different model
set can be selected with `--models-json`; the existing module-03 calibration
JSON is also accepted because its slots use `model_path` and `norm_path`.

Example:

```bash
python receiver/standard_receiver.py \
  --port /dev/cu.usbmodemXXXX \
  --plot \
  --output run/coinft_samples.jsonl \
  --csv-output run/coinft_samples.csv
```

For the current revised firmware's one-sensor output:

```bash
python receiver/standard_receiver.py \
  --port /dev/cu.usbmodemXXXX \
  --sensor-count 1 \
  --single-side left \
  --single-format single-legacy24
```

The receiver sends only `i` and `s` control commands. It does not send `t`;
zeroing is performed from the host-side raw stream.
