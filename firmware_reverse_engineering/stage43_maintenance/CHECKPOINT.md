# Stage 43 checkpoint

- Stage: `43_maintenance_commands`
- Parent: `42_official_capsense_rewrite` (retained unchanged)
- Acceptance IDs advanced: `CMD-09/11/15/17`, `EEPROM-03/04/06/07/08`,
  `STRUCT-02/09/10`
- Validation level: `L3 LIVE-SUBSET`
- Evidence level: static integration plus protected legal-`t` and `w/n` board runs
- Expected behavior: see `EXPECTED_BEHAVIOR.md`
- Named test: `make clean verify`
- Result: static build passed on 2026-09-03; legal `t` and `w/n` live subsets passed
- ELF size: text 19,132 B; data 1,224 B; bss 31,560 B (includes linker heap)
- BIN size: 20,992 B
- BIN SHA-256: `adf5a294d1fcc956d836b697c90e1ff0b1030e12168fe137d634363094c94e9f`
- HEX SHA-256: `3106c6cc94ba3f3a6749374aeeea8317d7f8bded2daa036ad0092a3b00635c84`
- EEPROM initial region: factory-equal 512 zero bytes at `0x5000..0x51ff`
- Frame SHA-256: `2f0ba501362c4b81bd7cf6a9a9518651311b52f936b381f61373c13e8104758c`
- Hardware/readback/factory restore: legal `t` and `w/n` runs completed; factory image restored and `q -> 0x1a` passed
- Detailed maintenance-image run: `verification/live/runs/20260903-072357_stage43_detailed/`.
  The maintenance image was first checked against a restored factory readback,
  then programmed and verified. A 100-frame stream measured 200.616 Hz with
  zero stop-tail bytes. A SWD reset issued while streaming left zero bytes
  after the reset settling window; 100 frames after reset measured 200.589 Hz.
  Three additional 50-frame start/stop cycles measured 200.594, 200.593 and
  200.603 Hz, each with zero stop-tail bytes. The board was finally re-flashed
  with a clean Stage 43 image, clearing the test EEPROM writes; the final
  20-frame capture measured 200.589 Hz.
- Extended legal `t` run in the same directory: 100 frames before and after
  `t 00 A5 15 1F` remained at 200.59 Hz. Channel 0 changed from median 19499
  to 17890; the largest unselected-channel delta was 6.0. After reset, channel
  0 remained 17889.5 and all channels stayed within 2.0 counts of the
  post-`t` capture. The post-`t` full readback differed from the candidate only
  inside the allowed Em_EEPROM window.
- Base configuration synchronized from live-validated `43_factory_base_config_match`:
  `modClk=1`, PRS source, sense divider 16 and `proxTh=100`.
- Known exclusion: SAR; invalid-`t` memory corruption; all live equivalence
- L3 preparation: four guarded workflows now exist under
  `../verification/live/`; Python/Shell syntax, parser boundaries, positive
  comparisons and negative rejection cases pass offline.
- Factory checker replay: the latest Stage 39 factory readback still matches
  its full HEX image outside the recorded 11 changed bytes in
  `0x5100..0x51ff`.
- Legal `t` evidence: `verification/live/runs/20260903-045748_stage43_t/`.
  Twenty frames remained at 200.49 Hz; channel 0 median changed from 19480.5
  to 17869.5, unselected-channel maximum delta was 6.5, and after reset the
  channel 0 median was 17851.0. Full readbacks differed only in the allowed
  Em_EEPROM window.
- `w/n` evidence: `verification/live/runs/20260903-050144_stage43_name/`.
  Default `n` returned `NFT\r\r\n`; `w COINFT\r` returned `COINFT\r\r\n`, and
  the same value was returned after reset. Full readbacks again differed only
  in `0x5000..0x51ff`; the factory image was restored afterward.
- Remaining safe steps: exercise single-MSC/single-slot isolation, direct
  `LAST`/EOS/ISR observation, complete Em_EEPROM error/CRC behavior, and
  long-run command interleaving. The detailed reset test proves reset and
  repeated timer/stream transitions, but does not prove the factory's exact
  microsecond-level EOS/LAST ordering. No invalid `t` index was sent.
