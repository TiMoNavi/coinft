# Stage 44 expected behavior

The default reset state is idle, with the Stage 43 CapSense and Em_EEPROM
initialization behavior. The legacy `q`, `s`, `i`, `t`, `w`, and `n` commands
retain their Stage 43 semantics and raw frame format.

`z` performs one complete phase-0/phase-1 scan pair, stores those 12 raw
counts as a volatile temporary tare, runs one inference, and replies with the
single byte `Z`. `p` performs the same scan pair and replies with a 114-byte
diagnostic record. `r` replies with the latest record without starting a scan.
Before the first `z` or first scan-completed inference, no diagnostic record is
returned by `r`.

The main loop invokes inference once after each completed scan pair. The raw
timer interrupt remains responsible only for the existing raw stream and GPIO
indicator. The MLP does not execute from an ISR.

Static acceptance requires:

- the `.mlp_model` section starts at `0x5200` and stays inside 384 KiB Flash;
- all 13,256 weights are linked as int8 data in Flash;
- actual application static RAM remains below 8 KiB, in addition to the fixed
  1 KiB stack reservation;
- no floating-point helper symbols are linked;
- Python and native-C golden vectors agree exactly;
- the historical 0019 capture has no activation saturation and stays within
  the recorded quantization error thresholds.

This is an embedded inference feasibility image, not a production calibration
release. It intentionally leaves the host-side stability/tare procedure,
per-device model pairing, axis/sign mapping, force bias, and moving average on
the host.
