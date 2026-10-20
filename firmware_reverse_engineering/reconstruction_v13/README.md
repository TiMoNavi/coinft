# CoinFT reconstruction v13

This stage binds the target-neutral scheduler from `08_reconstruction_v12/`
to the confirmed CY8C4149AZI-S593 CAT2 peripheral map in
`09_psoc4_target_hal/`.

## v13 scope

- dual MSCv3 instances at `0x40290000` and `0x402a0000`;
- SCB3 v3 register layout at `0x40270000`;
- six-word sensor-frame images recovered from the original SRAM snapshot;
- startup GPIO/HSIOM records recovered from the original HEX;
- host and MMIO-branch syntax verification without hardware access.

## Build checks

```text
cd ../09_psoc4_target_hal
make syntax
make syntax-mmio
make syntax-core
make test
```

These checks do not access KitProg, alter flash, or produce a flashable image.

## Read-only decoder

The v14 decoder prints the fifth-generation common-config candidates, the
resolved internal ISR callback, both channel descriptors, and all 14 runtime
six-word sensor frames:

```text
python3 tools/recover_msc_v14.py
```

It also decodes both runtime `cy_stc_msc_base_config_t` images at SRAM offsets
`0x59c` and `0x728`, reports their register names, mode tables, and instance
differences. The only observed base-config difference is `CSW_CTL` (`0x7e`
versus `0xf0`).

The resulting read-only C initializer is available at
`include/recovered_msc_base_config_v16.h`. It is deliberately not linked into
the target HAL until clock routing, pin mux timing, and interrupt sequencing
are independently verified.

The v18 host-only shadow model reconstructs `FUN_000012e8`, `FUN_000013d8`, and
`FUN_00001404`, including sensor/pin/shield CSW mask updates:

```text
python3 tools/recover_msc_v18.py
```

The captured snapshot produces `14/14 frames match all six words`. This is a
read-only validation artifact, not a flashable target image.

The unified source-level pseudocode is in
`../07_analysis/coinft_firmware_pseudocode_v20.c`; its scope and remaining
candidate interfaces are documented in `../07_analysis/restoration_progress_v20.md`.

The v21/v22 pseudocode adds raw-limit, sensor reset, history, and baseline
initialization paths. The history verifier is:

```text
python3 tools/recover_sensor_pipeline_v22.py
```

It reproduces all `13/13` contiguous history records from the captured SRAM,
including the descending-order cross-record writes.

The v44 dynamic-state host model and its boundary tests are:

```text
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function \\
  tests/test_v44_dynamic_state.c -o /tmp/test_v44_dynamic_state
/tmp/test_v44_dynamic_state
python3 tools/recover_widget_context_v44.py
```

This covers the recovered sense-period/tuning mode transitions and ARM shift
semantics. It remains a read-only host replay; the callback ABI, baseline
history/filter timing, and final CapSense generator inputs are not yet closed.

## Gate before an ELF/HEX

The MSC global configuration, clock routing, dynamic pin switching, interrupt
timing, and exact generated CapSense callback ABI still require confirmation.
The v13 backend is therefore a reviewable reconstruction candidate only.
