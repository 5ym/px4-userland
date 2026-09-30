# AGENTS.md

## Source of truth

- Normative product requirements are in `SPEC.md`. Read the relevant section before implementation.
- If an observed device behavior conflicts with the frozen specification, update its version and rationale before changing the implementation.
- Supported device profiles and their verification status are defined by the current `SPEC.md` and `README.md`. Do not claim support for another related device without a versioned specification change and the profile-specific hardware evidence required by `SPEC.md`.

## Supported scope

- Runtime targets are Linux (including environments where kernel modules cannot be installed), Android/Termux, and macOS.
- Every target runtime must support both the tuners and its internal card reader.
- Windows is outside the product scope. Windows users may use `tsukumijima/px4_drv`, which is a separate product with a different CLI and IPC interface.
- Android ad-hoc APK hardware testing is owned by dtv-android and is outside this repository's release gates and support claims. The APK is not a release artifact.
- FreeBSD is outside the product scope; validation-results.md entries for it are historical only.
- Firmware is not distributed, downloaded, extracted, or transformed by this repository.

## Implementation rules

- Keep the portable core C++17, exception-free and RTTI-free. Do not use glibc extensions or GNU-only APIs required for core functionality.
- Preserve glibc, musl, Bionic API 24+, and macOS portability. Keep platform APIs outside the portable core.
- Use fixed-width integers and checked lengths at USB, firmware, IPC, ATR, APDU, and TS boundaries.
- Preserve existing tests. Do not change expectations, fixtures, mocks, or skips merely to make a failure pass; add tests for new behavior.
- Do not reintroduce Linux kernel modules, chardev/ioctl interfaces, DKMS/Debian packaging, legacy udev rules, Windows artifacts, or implementations for devices outside the supported models.
- Physical USB changes, card insertion/removal, antenna changes, power changes, and other hardware operations require user confirmation before execution.
- Preserve existing dirty-tree work. Use `apply_patch` for edits and do not reset, checkout, or broadly reformat unrelated files.
- Do not run `git add`, `git commit`, or `git push` unless the user explicitly requests that operation.
- Create public issues only for unresolved problems known at publication time; do not create preventive placeholder issues.

## Release notes

- GitHub Release のタイトルは `vX.Y.Z` のみとし、先頭に製品名などを付けない。
- 本文の先頭見出しは `px4-userland vX.Y.Z` とする。
- 概要は敬体（です・ます調）で簡潔に書く。
- 「主な変更」「検証」「既知の制限」は箇条書きの常体で書く。該当項目がない節は省略し、埋め草を入れない。
- 「謝辞」は敬体で書く。
- リリースノートは利用者向けの変更概要と必要な注意に絞る。内部監査記録、詳細な試験ログ、ハッシュ一覧、余計な検証 matrix は載せない。環境別の詳細が必要な場合は README 等の正本へリンクし、matrix を複製しない。

```markdown
# px4-userland vX.Y.Z

[概要を敬体で簡潔に記載]

## 主な変更
- [変更点を常体で記載]

## 検証
- [検証結果を常体で簡潔に記載]

## 既知の制限
- [必要な場合のみ、常体で記載]

## 謝辞
- [貢献者への謝意を敬体で記載]
```

## Stable release validation

- Stable 公開前の検証は [`docs/release-validation.md`](docs/release-validation.md) の順に行う。判定条件の正本は `SPEC.md` の10章であり、手順書との不一致はSPECを優先して手順書を直す。
- 全OS・全機種の一律回帰を行わない。毎回必須のCI/candidate auditとcanaryを行い、SPEC 10.5.1/10.5.2のtriggerが成立する場合だけtargeted検証とlong soakを行う。long soakはreleaseあたり最大1つのruntime/access pathで実施し、時間経過・release回数だけを理由にした周期gateは設けない。独自の巨大検証scriptを追加しない。
- 検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一し、証拠は [`docs/platforms/validation-results.md`](docs/platforms/validation-results.md) へ記録する。実施・省略・非該当の根拠と失敗試行を残す。物理USB/cardの抜差しはユーザーの確認なしに行わない。
- Android ad-hoc APKの実機検証はdtv-android所管であり、本リポジトリのrelease gateに含めない。Windowsは対象外、FreeBSDも対象外とする。

## Handoff requirements

- Report changed files, commands run, results, and unverified scope at the end of each increment.
