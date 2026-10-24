# PX4既存PRの採否・後継（2026-10-24）

## PR #78 — PX4の受信・制御・カード処理を型付きUDTVへ接続

- 対象head: `20decedad439654475598edf40adf01fbdf90d82`（`wip/userland-ctl-parity-20261020`）。
- 判定: PR全体は採用しない。旧Core固定値・旧UDTV server/clientと、未完了の大規模統合を現在のmainへそのまま載せない。
- 現在の接続先: 現mainの公開PX4 CLI・Q3U4 backend・CARD/PCSC動作を維持する。`px4-ts`の共通Core consumer化はPR #85（head `13418ef36d2e286cb90ea80ae8b250df39f2707f`）へ移した。Core daemon/Context接続は製品Issue #86で継続中。
- 未回収: PR #78の全機能・試験をmainに採用済みとは扱わない。特にdaemon/control client/CARD経路の内部共通化はIssue #86およびその後継PRで検証する。
- 状態: OPENのまま。元PRのQ3U4確認は当時のCore世代・head限定であり、現mainやPR #85の実機証拠には転用しない。PR本文に記録された削除済みフィールド参照のCI失敗と未完了修正も保持する。

## PR #79 — PX4の共有runtime・TS・daemonと対応ソースを開発枝へ集約

- 対象head: `0719c6e5c9dd04c36d904faca0859c03ec620ae0`（`wip/userland-host-daemon-20261020`）。
- 判定: Core 352固定のWIP全体は採用しない。共有runtime/daemonのAPI世代が現Coreと異なるため、過去のテスト・実機結果を現接続の証拠として流用しない。
- 現在の接続先: TS consumerはPR #85、daemon/backend/Context接続はIssue #86へ切り分けた。PR #85は全CI成功後も、daemonとの同一UDTV wire組合せを確認するまで単独mergeしない。
- 未回収: Windows runtime/WinSCard移行、共有ctl、CARDの製品実機確認、完全なdaemon接続は現mainで置換済みとしない。現mainの既存PCSC/WinSCard機能は維持し、共通化後も個別に検証する。
- 状態: OPENのまま。元branch/headとPR本文のCore 352試験・CI失敗を記録として保持し、後継実装が全対象機能を置換した段階で再判定する。

## 参照

- [現行TS接続 PR #85](https://github.com/Khronos31/px4-userland/pull/85)
- [px4-ts共通Core移行 Issue #84](https://github.com/Khronos31/px4-userland/issues/84)
- [px4d共通Core Context接続 Issue #86](https://github.com/Khronos31/px4-userland/issues/86)
