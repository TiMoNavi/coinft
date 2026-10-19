# CoinFT calibration capture

`coinft_sync_capture.py` is the recommended direct-PSoC capture path. It uses
the same startup contract as the firmware acceptance tools:

```sh
python3 calibration/coinft_sync_capture.py capture \
  --port /dev/cu.usbmodem14403 --label factory \
  --output data/factory.json --frames 4000 --warmup 40
```

The result is a `coinft-raw-v1` JSON record with 12 raw channels, one host
monotonic timestamp per complete frame, measured rate, and the wall-clock time
of the first measured frame. The direct protocol is 1 Mbaud and has the form
`0x02 + 12 uint16 little-endian + 0x03`. The command checks `q -> 0x1A`, drops
warm-up frames, and sends `i` after capture.

## Timestamp synchronization

When an ATI logger writes a CSV with `timestamp_s` (or `time_s`, `Time`,
`time`) plus `Fx,Fy,Fz,Mx,My,Mz`, align it to the CoinFT frames with:

```sh
python3 calibration/coinft_sync_capture.py align \
  --capture data/factory.json --ati-csv data/ati.csv \
  --output data/factory_aligned.csv --max-skew-ms 3
```

For epoch timestamps, the capture wall-clock field is used directly. For two
relative logs, pass `--ati-offset-s` as the ATI time at the first CoinFT
frame. The tool never synthesizes a periodic PSoC sync waveform. If the ATI
CSV contains a `sync` column it is retained as an explicit cross-check, while
the primary join remains timestamp based.

## Loading and tare procedure

1. Mount the CoinFT and ATI so their coordinate frames and the `M_ARM` lever
   arm are fixed.
2. Start both loggers with the same host clock and record at least 2 s unloaded
   before applying force.
3. Apply a known sequence of unloaded, +/− force, +/− torque, and combined
   loads. Hold each load long enough to cover several CoinFT frames.
4. Use the first unloaded segment for tare only. Do not recompute the baseline
   during a loaded segment.
5. Align timestamps, inspect nearest-sample skew, fit the calibration model,
   and validate on a second load sequence. Save raw, ATI, model, and
   calibration metadata together.

The existing `coinft_data_collection.py` requires NI-DAQmx and h5py and still
contains its historical synthetic six-frame sync fallback. It is retained for
legacy datasets but is not evidence of physical synchronization; use the
timestamped tool above for new calibration runs.

## Baseline drift acceptance

For every firmware candidate, collect an unloaded capture of at least 5 s and
compare it with factory captures made before and after programming. Record:

- measured frame rate and frame count;
- per-channel median, standard deviation, P05/P95;
- linear slope in raw counts per second;
- first-decile versus last-decile median change.

The candidate is not accepted merely because its slope is small. It must stay
within the factory variability for the same board and temperature, or the
difference must be explained by a controlled thermal or mechanical change.

## Model sanity check

The ONNX model is a fixed single-frame model (`[1,12] -> [1,6]`). Run it one
frame at a time, using the unloaded beginning of a capture for the input tare:

```sh
python3 calibration/coinft_model_check.py \
  --capture data/factory.json \
  --model hardware_configs/CFT24_MLP.onnx \
  --norm hardware_configs/CFT24_norm.json \
  --output data/factory_model.json
```

This checks finiteness and reports six-axis median, standard deviation, and
span. It is a model/transport sanity check only; it does not establish force
or torque accuracy without the ATI loading experiment above.
