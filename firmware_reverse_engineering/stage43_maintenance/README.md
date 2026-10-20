# Stage 43: maintenance commands and persistence

Stage 43 keeps the corrected Stage 43 factory-base official CapSense acquisition
configuration and adds the
remaining factory maintenance commands in ordinary application C:

- `t` updates reference and compensation CDAC values, rebuilds baselines and
  sensor frames, and stores the two 12-byte parameter tables;
- `w` stores a 12-byte device name and falls through to `n`;
- `n` restores/creates the default `NFT\r` name and emits the factory
  `CR CR LF` ending;
- startup restores the saved scan parameters up to the first zero in table A;
- `q`, `n`, and `w` use the same best-effort, non-blocking single-byte UART
  writes as the factory firmware.

The storage implementation is Infineon `emeeprom release-v2.30.1` over
`block-storage release-v1.0.1`. Its 12-byte configuration is the recovered
factory configuration: 128 logical bytes, extended mode, wear factor 2, no
redundant copy, blocking writes, physical address `0x5000`.

The linker reserves exactly `0x5000..0x51ff`. The initial 512 zero bytes are
byte-identical to the same region in the factory BIN. Flashing the complete
Stage 43 HEX therefore clears previously saved CoinFT parameters, just as
restoring the complete factory HEX does. Runtime writes subsequently persist
across reset.

The recovered `t` descriptors now have a named-field explanation. In the
factory CapSense ABI, a 28-byte common context is followed by 13 widget
contexts of 56 bytes. Descriptor `0x0500004A + index * 0x38` therefore selects
widget `cdacRef` at offset 46. The sensor array follows at offset `0x2f4`, so
descriptor `0x010002FD + index * 10` selects sensor `cdacComp` at offset 9.
The Stage 43 implementation writes those fields directly and patches only the
corresponding fields of the known-good factory frame table.

Build and run all non-hardware checks with:

```sh
make clean verify
```

Outputs are in `build/out/`. `make verify` checks the linker ranges, startup
copy address, exact factory EEPROM bytes, exact frame-table hash, Em_EEPROM
configuration, and required ISR/storage symbols.

This is an `L3 LIVE-SUBSET` maintenance integration candidate. Its acquisition
configuration matches the live-validated `43_factory_base_config_match` stage.
Protected board runs have now exercised legal channel-0 `t` and the `w/n`
name path, including reset persistence and complete Flash readback. The
factory image was restored after each run. This does not yet establish CRC
corruption recovery, power-loss behavior, complete ISR/EOS equivalence, or
full command-interleaving stability. The corrected
`43_factory_base_config_match` directory remains the default-acquisition
fallback.

For memory safety, Stage 43 does not reproduce the factory `t` out-of-range
SRAM/Flash corruption. Indices 0 through 11 follow the factory path; an invalid
index leaves both parameter tables unchanged but still performs the EEPROM
rewrite and CapSense re-enable/tare. This makes Stage 43 a safe functional
rewrite, not a byte-for-byte clone of the factory vulnerability.

SAR remains excluded: the factory image has been observed with `chan_en=0`,
and no evidence currently connects SAR to the normal 12-channel raw output.

Live checks are intentionally split into two destructive groups under
[`../verification/live/`](../verification/live/README.md): legal `t` plus
reset recovery, and `w/n` plus reset persistence. Both scripts compare full
Flash outside `0x5000..0x51ff` and restore the factory HEX from an exit trap.
The completed evidence directories are
`verification/live/runs/20260903-045748_stage43_t/` and
`verification/live/runs/20260903-050144_stage43_name/`. The base
configuration is shared with the live-validated acquisition stage; remaining
maintenance work is limited to untested error, power-loss and stress paths.
