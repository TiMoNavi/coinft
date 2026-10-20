# Expected behavior and acceptance state

## Runtime contract

| Input/state | Expected behavior |
| --- | --- |
| reset | Initialize CapSense and Em_EEPROM, restore consecutive nonzero-A scan parameters, create `NFT\r` if no saved name contains CR, then remain idle. |
| `q`, idle | Best-effort single-byte response `1a`. |
| `s`, idle | Start the same two-phase CapSense loop as Stage 42 and emit 26-byte raw frames at the timer rate. |
| `i`, streaming | Complete the phase selected before the byte was consumed, then stop and return idle. |
| `t index ignored A B` | Block until all four parameter bytes arrive, wait for CapSense idle, save/apply `cdacRef=A` and `cdacComp=B`, write 24 bytes at EEPROM offset 0, re-enable/tare, and restore the previous mode. |
| `w bytes...`, idle | Block until CR or 12 bytes, force byte 11 to zero, write 12 bytes at EEPROM offset `0x40`, then execute `n`. |
| `n`, idle | Read/repair the name, send bytes through CR, then send an additional CR and LF. |

`t` is recognized in idle and streaming. `w` and `n` are recognized only in
idle. During streaming, other command bytes are consumed and ignored.

## Acquisition invariants

- UART frame: `02`, twelve little-endian raw counts, `03`.
- Phase 0: reference slot 6 state 2, then `ScanSlots(0, 4)`.
- Phase 1: reference slot 6 state 0, pre-wait, then `ScanSlots(4, 2)`.
- Frame table starts from the SWD-exported 14 x 6 words with packed SHA-256
  `2f0ba501362c4b81bd7cf6a9a9518651311b52f936b381f61373c13e8104758c`.
- `t` changes only `SNS_CDAC_CTL.SEL_RE` and `SEL_CO` for the selected output
  channel; all other recovered frame bits remain fixed.

## Persistence invariants

- Configuration ABI is 12 bytes and contains `128, 0, 2, 0, 1, 0x5000`.
- Physical storage is exactly 512 bytes at `0x5000..0x51ff`.
- The complete Stage 43 HEX initializes this region to the same 512 zero bytes
  as the factory BIN.
- Parameter restore stops at the first `A[channel] == 0`.
- Read/write results after successful middleware initialization are ignored at
  the same application call sites as the factory firmware.

## Acceptance state

| Item | State |
| --- | --- |
| Clean ARM build | passed |
| Static linker/data/frame/EEPROM checks | passed by `make verify` |
| Official Em_EEPROM and block-storage linked | passed |
| Named `t` descriptor field mapping | statically closed |
| `q/s/i/t/w/n` application state machine | implemented, machine-code control flow reviewed |
| Default raw values equal to factory | Stage 43 factory-base live subset passed at about 200 Hz; this maintenance image uses the same corrected configuration |
| `t` raw effect and tare equal to factory | Legal channel-0 live subset passed; selected median changed while unselected channels stayed within 6.5 counts; final factory differential remains open |
| `t/w/n` reset persistence | `t` channel-0 persistence and `w/n` name persistence passed in protected runs |
| EEPROM CRC/version/Flash changes | Write-window/readback checks passed; CRC/version, dual-region selection and failure paths remain open |
| Long-run command interleaving | pending board test |

The legal-`t` and `w/n` live round-trip scripts are implemented under
[`../verification/live/`](../verification/live/README.md). Script syntax,
positive checker cases and negative checker cases pass offline. The protected
runs are:

- `../verification/live/runs/20260903-045748_stage43_t/`
- `../verification/live/runs/20260903-050144_stage43_name/`

Both contain UART evidence and complete Flash readbacks, and both restore the
factory image before exiting. These are legal-command live subsets, not proof
of CRC corruption recovery, power-loss behavior, or complete ISR equivalence.

The invalid-`t` memory corruption is intentionally not reproduced. All other
claims in this document apply only to legal channel indices 0 through 11.
