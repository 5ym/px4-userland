# Source provenance and rights record

`px4-userland` is an independent standalone repository containing derivative portions.
This document preserves the engineering provenance that was previously visible partly through the GitHub
fork relationship. It is not a legal opinion and does not replace the notices in individual source files or the
terms in [`LICENSE`](LICENSE).

## Repository lineage

- Direct source: [`tsukumijima/px4_drv`](https://github.com/tsukumijima/px4_drv), snapshot commit
  `9eedea8c502875a788697984b93b50032339b9aa`.
- MLT5-family increment (2026-09-24): PX-MLT5PE/DTV02A-5TS-P support is derived from the same repository at
  `d748866f0da1cb3656106a520de4e9d7f073aacd` (tag `v0.6.1`, which contains DTV02A-5TS-P commit
  `72a807de2009c2ce376953c75687b4d45708f00e`). Files changed or added in that increment cite `v0.6.1` in their
  headers; untouched files keep the original snapshot. Between the two snapshots, the origin paths used by this
  project differ only in `driver/px4_usb.c` and `driver/px4_usb.h` (the DTV02A-5TS-P product ID).
- MLT J.83 Annex B increment (2026-10-26): the J.83B (5.6 Msps) demodulator sequence is based on
  [`tsukumijima/px4_drv` pull request #46](https://github.com/tsukumijima/px4_drv/pull/46) at head commit
  `2ce5304043d5efd47142ab247e6b815a16302c93` (branch `feature/isdb-c-j83b`, open and unmerged when this was
  recorded), and the register values come from `koreapyj/cxd28xx`. See
  [MLT J.83 Annex B increment](#mlt-j83-annex-b-increment).
- Original upstream: [`nns779/px4_drv`](https://github.com/nns779/px4_drv).
- Current product: `Khronos31/px4-userland`, standalone libusb userspace drivers and tools
  for PLEX and e-Better tuners on Linux, Android, macOS, and Windows. See
  [README](README.md) and [SPEC](SPEC.md) for implemented profiles, and
  [platform validation results](docs/platforms/validation-results.md) for the tested scope.
- License for this project and its derivative portions: [`GPL-2.0-only`](LICENSE).

Git author names and commit history are evidence about repository activity; they are not, by themselves, a list of
copyright holders. In particular, a contributor, git author, maintainer, project name, or implementation agent is
not added as a copyright holder here without a separate rights basis. The retained `Copyright (c) 2018-2021 nns779`
line appears only where the audited origin file explicitly contained that notice and the audit found its substantive
expression retained. The tsukumijima-derived portions without a file-local copyright notice are described with a
source-snapshot maintenance fact without guessing a copyright holder. For the smart-card-derived `card.h`,
`card.cpp`, and `card_tests.cpp`, the audit records that the relevant WinUSB source files were committed/maintained
in the direct-parent history by tsukumijima; that is a commit-author and maintenance fact, not a copyright-holder
claim.

## MLT J.83 Annex B increment

The 2026-10-26 increment adds an internal ITU-T J.83 Annex B (5.6 Msps) path to the MLT-family frontend. It is a
reference draft that is not adopted into `SPEC.md`. The demodulator part is based on the J.83B portions of pull
request #46; its ISDB-C, TSMF, TLV, ioctl, and WinUSB/BonDriver changes were not used.

- Source of the register values: `cxd2878.c` in
  [`koreapyj/cxd28xx`](https://github.com/koreapyj/cxd28xx/blob/c5fa27eecbe80633cbb365cc44656daea1c0e2e3/cxd2878.c)
  at commit `c5fa27eecbe80633cbb365cc44656daea1c0e2e3`, which pull request #46 also cites. Line numbers in this
  section refer to that commit unless another commit is named.
  - Demodulator: `SLtoACC()`, `SLtoACC_BandSetting()` for `SONY_DTV_BW_J83B_5_60_MSPS`, `tsRateCtrlOff = 0` for
    `SONY_DTV_SYSTEM_J83B` in `cxd2878_setTSClkModeAndFreq()`, the `SONY_DTV_SYSTEM_J83B` branch and the common
    tail of `cxd2878_sleep()`, and the lock bits of the DVB-C/J.83B branch of the status read (lines 5001–5011).
    These values are already in the repository's first commit `186decd` ("tbs5530", 2026-03-05); later commits
    reformat some of the lines with the same values. The J.83B demodulator code is not split by chip.
  - Tuner: the values are copied from the HELENE (CXD2858ER) code that commit `8427e4b` ("cxd2878: Add support
    for HELENE", 2026-03-07) added (from line 1613 of `c5fa27e`), because `cxd2878.c` states GPL-2.0-only: the
    values of `helene_tune_terr()` (lines 1822–1947) when `is_cable` is true for `SONY_FREIA_DTV_SKP_OPT`
    (conditions on lines 1815–1819, 1835–1843, 1847, 1864–1866, 1872–1882, and 1929–1935), the
    `SONY_HELENE_DTV_J83B_5_6` row of `g_terr_param_table_helene[]` (line 142), the `BW_6`/`OFFSET()`
    definitions in `cxd2878_priv.h` (lines 22–23), and the 50 ms wait on line 1943. 0x9c and 0x9d are written in
    one transfer as in the existing ISDB-T tune; `cxd2878.c` writes them separately with the same values. The
    HELENE table of `c5fa27e` (lines 113–143) has the same content as the table of `sony_helene.c` in
    `it930x_driver_native` `64fd4f3` (lines 40–70), comments included (the diff is empty apart from whitespace).
    GPL-2.0-only rests on the cxd28xx declaration.
  - Up to the cxd28xx main branch (`c5fa27e`), that HELENE code has never run in `cxd2878.c`:
    `helene_tune_terr()` and `helene_tune_sat()` are defined (lines 1709 and 1957) but called nowhere in any
    commit from `8427e4b` to `c5fa27e`; `cxd2878_set_frontend()` calls only `ascot3_tune()` and `freia_tune()`
    (lines 5530–5533). The chip detection has no CXD2856 case, so a CXD2856 (0x090) falls to the default branch
    and the attach fails (lines 5851–5854). The table lookup with an `enum sony_freia_tv_system_t` value is also
    wrong: J.83B 5.6 Msps is `SONY_FREIA_DTV_SKP_OPT` at index 29 (`cxd2878_priv.h` line 129), beyond the 29
    rows of the HELENE-ordered table (`SONY_HELENE_TERR_TV_SYSTEM_NUM`; the J.83B row is index 27). The
    unmerged branch `test/pxmlt` at `b837d8f` ("add support for pxmlt devices", 2026-09-24) adds the CXD2856
    detection, calls `helene_tune_terr()`/`helene_tune_sat()` from `cxd2878_set_frontend()`, and lists the
    PX-MLT family in its README, but it accepts only ISDB-T and ISDB-S (its lines 6080–6087), so the J.83B
    (`SONY_FREIA_DTV_SKP_OPT`) path still does not run there, the FREIA-enum lookup remains, and the cable values
    are unchanged from `c5fa27e`. The values are therefore supported by the comparison with Sony's
    `sony_helene.c` and pull request #46 below, not by `cxd2878.c` running them.
  - Chips: `cxd2878.c` detects CXD2857, CXD2878/CXD6801, CXD6802, CXD6822, and CXD2878A/CXD6821 (lines
    5836–5850), and `SONY_TUNER_IS_HELENE()` (`cxd2878.h` lines 35–38) selects the HELENE tuner for CXD2856 and
    CXD2857 demodulators (`cxd2878_priv.h` lines 241–242; HELENE is `SONY_ASCOT3_CHIP_ID_2858`, "CXD2858ER",
    line 251), with separate HELENE initialization on lines 4833 and 4894, although the detection of `c5fa27e`
    rejects CXD2856. The module table in its `README.md` at `c5fa27e` lists only CXD2878/CXD6801/CXD6802
    (`test/pxmlt` adds CXD2856 and the PX-MLT family). Operation on the MLT
    boards is not verified here.
  - `cxd2878.c` declares GPL-2.0-only (its first line reads `SPX-License-Identifier: GPL-2.0-only`) and carries
    `Copyright (c) 2022 Sony Semiconductor Solutions Corporation`, `Copyright (c) 2021 Davin zhang
    <Davin@tbsdtv.com> www.Turbosight.com`, and `Copyright (c) 2026 Yoonji Park <koreapyj@dcmys.kr>`. In
    `186decd` the file read `//SPX-License-Identifier: GPL-2.0-or-later` with only the Davin zhang notice; the
    GPL-2.0-only line and the Sony and Yoonji Park notices date from `5500068` (2026-03-06). These lines are
    retained in `cxd2856er.cpp`, `cxd2858er.cpp`, and the test, not in the headers, which hold
    declarations only. Following the policy above (a notice is kept where the origin states it and its
    substantive expression is retained), the notices are kept because these files follow not only the values
    but also the order and structure of the `cxd2878.c` write sequences. Pull request #46 describes `cxd2878.c`
    as TBS code that ports Sony's procedures; that description is used here for the demodulator part only.
- Comparison only: pull request #46 (`driver/cxd2858er.c`, which cites Sony's `sony_helene.c` and
  `sony_tuner_helene.c`), and `sony_helene.c` (`api/DW_Frontend/CXD2856/tuner/terr_cable_sat_helene/refcode/`),
  `sony_tuner_helene.c` (`api/DW_Frontend/CXD2856/tuner/terr_cable_sat_helene/`), `sony_demod_isdbc.c`
  (`api/DW_Frontend/CXD2856/isdb_cable/`), and `CXD2856.c` (`api/DW_Frontend/CXD2856/`) in
  [`soralis0912/it930x_driver_native`](https://github.com/soralis0912/it930x_driver_native/tree/64fd4f3fd04cd8c19855250ebd88278734d4eba6)
  at commit `64fd4f3fd04cd8c19855250ebd88278734d4eba6`. Pull request #46 refers to that repository as PLEX's
  Linux driver; its `Makefile` line 1 reads `module=usb-plex` and `src/it930x-core.c` line 2406 names the driver
  `"usb-plex"`, but the attribution is not verified here.
  - The tuner values match `sony_helene.c`, which is what supports them: the `SONY_HELENE_DTV_J83B_5_6` row
    of the CXD2856-generation table (line 69) and `TER_tune()` (0x91 on lines 1144–1152, 0x9c/0x9d in one
    transfer on lines 1156–1169, REF_R 0x10 on lines 1208–1219, 0x63–0x66 on lines 1265–1283, 0x75–0x78 on
    lines 1369–1378), the 0x88/0x87 writes of `TER_tune_end()` (lines 1398–1403), and the 50 ms wait in
    `sony_tuner_helene.c` line 300. These line numbers refer to `it930x_driver_native` at `64fd4f3`.
  - `sony_demod_isdbc.c` names bank 0x40 register 0x14 `OREG_QAM_ENABLE` (line 467) and writes 0x14 to it for
    ISDB-C (line 485). The px4-userland files do not use that name and do not write 0x14 to that register.
  - No value or expression is taken from these files; they were used only for comparison, so their notices are
    not added.
- The TS clock for J.83B keeps the existing PX-MLT ISDB-S clock and only enables gap insertion
  (`OTSRATECTRLOFF = 0`), as pull request #46 does. Pull request #46 matches it to the terrestrial/cable TS clock
  that `api/DW_Frontend/CXD2856/CXD2856.c` of `it930x_driver_native` `64fd4f3` sets (gated clock on line 204,
  38.4 Mbps on line 220, overwritten with 64 Mbps on line 224); those lines were compared, nothing is taken.
- The J.83B sleep stops the TS output with the existing ISDB-T/S steps (without the `0xa9` read of
  `cxd2878_setstreamoutput()`) and then makes the `cxd2878_sleep()` writes. Those restore only part of what the
  J.83B wakeup writes.
- Pull request #46 omits the bank 0x10 select at the start of `SLtoACC_BandSetting()`; this increment keeps it as
  `cxd2878.c` does.
- Pull request #46 records that its author checked J.83B only by I2C simulation (`docs/20261018_isdbc_validation.md`
  line 121). The same document (lines 179–182) and `docs/j83b_winusb_experimental.md` line 3 of aoiro-labo/px4_drv
  commit `55917e5`, which pull request #46 merged, record partial reception on real boards with the TS clock used
  before pull request #46 changed it in `8023596`. The TS clock of this increment is the changed one, which has no
  real-board record. This increment is offline-tested only.

| Current file | Class | Origin path(s) | Retained notice | Change overview / notice date |
|---|---|---|---|---|
| `userland/src/cxd2856er.cpp` | mixed | `cxd2878.c` (koreapyj/cxd28xx `c5fa27e`); `driver/cxd2856er.c` (pull request #46 `2ce5304`) | `cxd2878.c` notices | J.83B TS clock, wakeup, sleep, and lock check; 2026-10-26 |
| `userland/src/cxd2856er.h` | mixed | declarations for the above | none added (declarations only) | J.83B system and declarations; 2026-10-26 |
| `userland/src/cxd2858er.cpp` | mixed | `cxd2878.c` HELENE code (koreapyj/cxd28xx `c5fa27e`, added in `8427e4b`); compared with `driver/cxd2858er.c` (pull request #46) and `sony_helene.c` | `cxd2878.c` notices | J.83B tune and terrestrial stop from J.83B; 2026-10-26 |
| `userland/src/cxd2858er.h` | mixed | declarations for the above | none added (declarations only) | J.83B system and declaration; 2026-10-26 |
| `userland/src/mlt5pe_frontend.cpp` | mixed | `driver/pxmlt_device.c` (pull request #46 `2ce5304`); lock check compared with `PxMltReceiver::CheckLock()` in `winusb/src/DriverHost_PX4/pxmlt_device.cpp` (pull request #46 `2ce5304`) | none added | J.83B tune and lock dispatch; 2026-10-26 |
| `userland/src/mlt5pe_frontend.h` | mixed | `driver/pxmlt_device.c` (pull request #46 `2ce5304`); lock check compared with `PxMltReceiver::CheckLock()` in `winusb/src/DriverHost_PX4/pxmlt_device.cpp` (pull request #46 `2ce5304`) | none added | J.83B lock declaration; 2026-10-26 |
| `userland/tests/mlt_j83b_tests.cpp` | mixed | `cxd2878.c` (koreapyj/cxd28xx `c5fa27e`); `driver/cxd2856er.c`, `driver/cxd2858er.c` (tsukumijima/px4_drv `d748866`, v0.6.1) for the TS clock, TS output, post-tune, and tuner stop steps | `cxd2878.c` notices; `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Expected J.83B I2C sequences plus ISDB-T/S non-change checks; 2026-10-26 |

## dtv-userland-core foundation extraction

The first foundation increment extracts audited independent core types into a separate MIT-licensed library,
`dtv-userland-core`, used by this product through the pinned commit in `cmake/dtv-userland-core.revision` (and the
`dtv::core` CMake target). The product no longer carries its own `userland/src/error.cpp`, `userland/src/logging.cpp`,
or `userland/src/mock_transport.cpp` implementations; `userland/include/px4/{error,logging,transport,mock_transport}.h`
now alias the shared `dtv::userland` types, while the product keeps its device endpoint/transfer constants and a
default-constructible `MockTransport` facade that supplies the PX4 policy explicitly. `userland/include/px4/firmware.h`
befriends `dtv::userland::Result` because `Result` is now the shared type. The product remains GPL-2.0-only; the
foundation library carries the MIT license recorded in its own `LICENSE`/`PROVENANCE.json`, which the
corresponding-source archive bundles under `repository/third_party/dtv-userland-core`.

## Windows stdin EOF monitor

The product daemon and its Windows monitor tests use
`px4::userland::cli::StdinEofMonitor` from the pinned Core `dtv::host_facade`
target. The earlier product-local `px4d_windows_stdin` implementation was
removed after its shared implementation was accepted in Core; the opt-in flag,
stop callback, and cleanup sequence remain unchanged.

## Date and audit scope

The audit covered all 84 files under `userland/`: 17 adapted, 11 mixed, one uncertain, and 55 original under the
audit's engineering classification. The 28 adapted/mixed files below received a prominent file-local notice. Each
date is an estimated port/change date derived from that file's pre-edit mtime calendar date, cross-checked
against the project memory's Increment 3A–4B work recorded on 2026-09-02 and 2026-09-03. These dates are not the
edit date of this provenance pass and are not claims about the exact historical authorship, creation date, or
copyright date of the source. The unavailability of a
complete creation history for the pre-commit userland files is an explicit ambiguity.

## Complete adapted/mixed mapping

The following table has exactly one row for each of the 28 files classified as adapted or mixed by the audit. The
paths in the Origin path(s) column are paths in the direct parent snapshot above.

| Current file | Class | Origin path(s) in direct parent | Retained notice | Change overview / notice date |
|---|---|---|---|---|
| `userland/include/px4/card.h` | adapted | `winusb/src/DriverHost_PX4/card_device.hpp`; `winusb/src/DriverHost_PX4/smart_card.hpp`; `winusb/src/DriverHost_PX4/smart_card.cpp`; `winusb/src/WinSCard_PX4/bcas_atr.hpp` | No file-local copyright notice in the source snapshot; no holder inferred | Portable card API and card-state declarations for ATR/T=1 support; 2026-09-03 |
| `userland/include/px4/it930x.h` | adapted | `driver/it930x.c`; `driver/it930x.h`; `driver/itedtv_bus.c`; `driver/px4_device.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Portable IT930x command/register, firmware, power, and card-UART interface; 2026-09-03 |
| `userland/src/bridge_i2c.cpp` | adapted | `driver/i2c_comm.h`; `driver/it930x.c`; `driver/tc90522.c` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Portable I2C request, repeater, length, and command sequencing; 2026-09-02 |
| `userland/src/bridge_i2c.h` | adapted | `driver/i2c_comm.h`; `driver/it930x.c`; `driver/tc90522.c` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Portable bridge-I2C interface and bounded request definitions; 2026-09-02 |
| `userland/src/card.cpp` | adapted | `winusb/src/DriverHost_PX4/card_device.hpp`; `winusb/src/DriverHost_PX4/smart_card.hpp`; `winusb/src/DriverHost_PX4/smart_card.cpp`; `winusb/src/WinSCard_PX4/bcas_atr.hpp` | No file-local copyright notice in the source snapshot; no holder inferred | Portable ATR and T=1 state machine over the card transport; 2026-09-03 |
| `userland/src/it930x.cpp` | adapted | `driver/it930x.c`; `driver/it930x.h`; `driver/itedtv_bus.c`; `driver/px4_device.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Portable IT930x control, firmware initialization, register, power, and UART operations; 2026-09-03 |
| `userland/src/it930x_protocol.cpp` | adapted | `driver/it930x.c` | `Copyright (c) 2018-2021 nns779` retained from the explicit `driver/it930x.c` notice | Portable scatter-image and command-frame parser; 2026-09-02 |
| `userland/src/it930x_protocol.h` | adapted | `driver/it930x.c` | `Copyright (c) 2018-2021 nns779` retained from the explicit `driver/it930x.c` notice | Portable protocol parser contract and bounded views; 2026-09-02 |
| `userland/src/q3u4_frontend.cpp` | adapted | `driver/px4_device.c`; `driver/px4_device.h`; `winusb/src/DriverHost_PX4/px4_device.cpp`; `winusb/src/DriverHost_PX4/px4_device.hpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/px4_device.*` notices | Portable Q3U4 receiver mapping, open/tune/TSID/capture, power, and cleanup flow; 2026-09-03 |
| `userland/src/q3u4_frontend.h` | adapted | `driver/px4_device.c`; `driver/px4_device.h`; `winusb/src/DriverHost_PX4/px4_device.cpp`; `winusb/src/DriverHost_PX4/px4_device.hpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/px4_device.*` notices | Portable frontend and receiver lifecycle declarations; 2026-09-03 |
| `userland/src/r850.cpp` | adapted | `driver/r850.c`; `driver/r850.h` | `Copyright (c) 2018-2021 nns779` retained from explicit R850 notices | Portable R850 tables, initialization, PLL, and tune sequence; 2026-09-02 |
| `userland/src/r850.h` | adapted | `driver/r850.c`; `driver/r850.h` | `Copyright (c) 2018-2021 nns779` retained from explicit R850 notices | Portable R850 interface and parameter definitions; 2026-09-02 |
| `userland/src/rt710.cpp` | adapted | `driver/rt710.c`; `driver/rt710.h` | `Copyright (c) 2018-2021 nns779` retained from explicit RT710 notices | Portable RT710/RT720 tables, PLL, bandwidth, and tune sequence; 2026-09-02 |
| `userland/src/rt710.h` | adapted | `driver/rt710.c`; `driver/rt710.h` | `Copyright (c) 2018-2021 nns779` retained from explicit RT710 notices | Portable RT710/RT720 interface and parameter definitions; 2026-09-02 |
| `userland/src/tc90522.cpp` | adapted | `driver/tc90522.c`; `driver/tc90522.h`; `driver/i2c_comm.h` | `Copyright (c) 2018-2021 nns779` retained from explicit TC90522/I2C notices | Portable TC90522 register, repeater, initialization, and capture sequence; 2026-09-02 |
| `userland/src/tc90522.h` | adapted | `driver/tc90522.c`; `driver/tc90522.h`; `driver/i2c_comm.h` | `Copyright (c) 2018-2021 nns779` retained from explicit TC90522/I2C notices | Portable TC90522 interface and register operation definitions; 2026-09-02 |
| `userland/tests/card_tests.cpp` | adapted | `winusb/tests/smart_card_state_test.cpp`; `winusb/src/WinSCard_PX4/bcas_atr.hpp`; `winusb/src/DriverHost_PX4/smart_card.cpp` | No file-local source copyright notice; no holder inferred | Portable mock/time tests for ATR and T=1 state/error scenarios; 2026-09-03 |
| `userland/src/identity.cpp` | mixed | `driver/px4_device.c`; `driver/px4_usb.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Portable serial parsing and strict Q3U4 grouping; Result-based validation is project adaptation; 2026-09-02 |
| `userland/src/libusb_transport.cpp` | mixed | `driver/itedtv_bus.c`; `driver/itedtv_bus.h`; `driver/px4_usb.c`; `driver/px4_usb.h`; `winusb/src/DriverHost_PX4/itedtv_bus_winusb.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | libusb transport, endpoint/pair lifecycle, fd wrapping, cancellation, and bounded teardown; 2026-09-03 |
| `userland/src/tagged_ts_demux.cpp` | mixed | `driver/px4_device.c`; `driver/ts_sync.h`; `winusb/src/DriverHost_PX4/px4_device.cpp`; `winusb/tests/ts_sync_condition_test.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/px4_device.c` notice; `ts_sync.h` alone carries no such notice | Portable tagged-TS synchronization, tag extraction, rewrite, bounded pending data, and recovery counters; 2026-09-02 |
| `userland/tests/frontend_tests.cpp` | mixed | `driver/tc90522.c`; `driver/r850.c`; `driver/rt710.c`; `driver/px4_device.c` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Legacy register/control golden sequences plus new mock and failure-matrix coverage; 2026-09-02 |
| `userland/tests/it930x_card_tests.cpp` | mixed | `driver/it930x.c`; `winusb/src/DriverHost_PX4/smart_card.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/it930x.c` notice | IT930x card-UART golden sequence plus portable mock/error coverage; 2026-09-03 |
| `userland/tests/q3u4_frontend_tests.cpp` | mixed | `driver/px4_device.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/px4_device.c` notice | Q3U4 initialization/tune/close golden order plus cleanup/error coverage; 2026-09-03 |
| `userland/tests/q3u4_power_tests.cpp` | mixed | `driver/px4_device.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/px4_device.c` notice | Bridge/card power interlock golden algorithm plus rollback/failure harness; 2026-09-03 |
| `userland/tests/r850_tests.cpp` | mixed | `driver/r850.c` | `Copyright (c) 2018-2021 nns779` retained from explicit R850 notice | Legacy R850 tables/selected outputs plus portable mock/failure tests; 2026-09-02 |
| `userland/tests/rt710_tests.cpp` | mixed | `driver/rt710.c` | `Copyright (c) 2018-2021 nns779` retained from explicit RT710 notice | Legacy RT710/RT720 initialization and output tables plus portable mock/failure tests; 2026-09-03 |
| `userland/tests/tagged_ts_demux_tests.cpp` | mixed | `driver/px4_device.c`; `driver/ts_sync.h`; `winusb/src/DriverHost_PX4/px4_device.cpp`; `winusb/tests/ts_sync_condition_test.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit `driver/px4_device.c` notice; `ts_sync.h` alone carries no such notice | Legacy sync predicate/golden behavior plus fragment, garbage, recovery, and counter tests; 2026-09-03 |
| `userland/tests/test_main.cpp` | mixed | `driver/it930x.c`; `driver/itedtv_bus.c`; `driver/px4_usb.c`; `driver/px4_device.c`; `winusb/src/DriverHost_PX4/itedtv_bus_winusb.c`; `winusb/src/DriverHost_PX4/px4_device.cpp` | `Copyright (c) 2018-2021 nns779` retained from explicit driver notices | Firmware/control framing, warm init, transport/grouping, and stream-lifecycle golden tests plus portable failure tests; 2026-09-03 |

## Firmware

The firmware binary is not included in the source tree, source archive, or release artifact. It is not a third-party
library. Runtime firmware preparation and licensing are separate from this source provenance record, and the project
does not download, extract, or transform vendor firmware.

`userland/src/firmware.cpp` was classified as `uncertain` at audit time. The post-audit conclusion recorded here and
in the file itself is that it is a project-original Increment 3A implementation: the available source history shows
no evidence that its SHA-256 representation was transcribed from the legacy `driver/` or `winusb/` trees. The
technical claim is limited to the standard FIPS SHA-256 constants and processing.

## Fork-network separation

GitHub fork-network separation is complete: `Khronos31/px4-userland` is an independent repository with no parent
relationship, and `main` is its default branch.

Removing the GitHub fork relationship does not change copyright, license, derivative-work, or corresponding-source
obligations. The fork badge is provenance metadata, not a substitute for file notices, this mapping, `LICENSE`, or
third-party release materials. Any later standalone import, squash, tag, or binary release must preserve these records
and must not reintroduce historical firmware or Windows signing artifacts into the current product.
