# Stage 44 checkpoint

- Stage: `44_integer_mlp_experiment`
- Parent: `43_maintenance_commands` (retained unchanged)
- Device: `CY8C4149AZI-S593`, Cortex-M0+, 48 MHz, 384 KiB Flash, 32 KiB SRAM
- MLP: `12 -> 128 -> 64 -> 36 -> 24 -> 12 -> 6`, 13,256 int8 weights
- Build: passed `make clean verify`
- ARM size: text 35,976 B; data 1,224 B; bss 31,560 B including linker heap
- Actual initialized/static RAM end: `0x20000e48` (about 3.7 KiB from RAM base)
- Model section: `0x00005200..0x00008e97` (15,512 bytes)
- Float helper symbols: none
- Host/C golden vectors: 11 vectors, exact integer agreement
- Historical 0019 replay: 2,012 samples, zero activation saturation
- Historical float comparison: force RMSE `0.00527/0.00806/0.01563 N`,
  max `0.0362/0.0480/0.1248 N`; torque RMSE below `0.00032 Nm`
- Live evidence: `verification/live/runs/20260905-095625_stage44_integer_mlp/`
- Live result: raw stream 200.521/200.598/200.546 Hz across three runs;
  target/Python exact match for 42 diagnostic frames; 0 saturation
- Inference duration: 245,147..250,297 cycles, approximately 5.11..5.21 ms
- Visual check: 55 diagnostics recomputed with the exact 0019 ONNX Runtime
  model; target-vs-ONNX plots and JSON summary generated in the live evidence
  directory; original `coin-ft/calibration/coinft_model_check.py` returned
  finite outputs on the post-restore Stage 43 capture
- Restore: exact pre-test full Flash readback restored and verified; Stage 43
  is running after the experiment
