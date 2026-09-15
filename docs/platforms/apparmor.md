# AppArmorで実行する際の注意

> [!NOTE]
> 本リポジトリにAppArmor profileは含まれない。本文書は一時的な自作profileによる実機検証の記録と運用上の注意である。

## 実行前の要点

- profileは利用者側で用意する。
- profile割り当て前の実行状態は `unconfined` となる。
- 起動後に `/proc/<pid>/attr/current` を確認し、対象profileの適用状態を確かめる。
- 検証対象は `px4d` をprofile拘束し、クライアント（`px4-ts` や `px4ctl`）を外部からUnixドメインソケット経由で接続させる構成である。
- `pcscd` 連携時は、拘束された `px4d` を介して IFD Handler および `pcscd` consumer 経路が動作する。`pcscd` 自体の実labelは `unconfined` であり、`pcscd` 専用profileによる拘束は検証対象外である。

## 検証環境

- 検証日: 2026-09-14（初回検証）、2026-09-15（完全再検証）
- ソフトウェア: `px4-userland` Stable v0.1.3 Linux glibc x86_64 配布アーカイブ（SHA-256: `5eabfa041c1d61230b31c0ed370d40f842a741d8523756c826b926f58155269b`）
- デーモンバイナリ: `px4d`（SHA-256: `0fa31583b8725183017c0f8b0e5eec437700da9a3d9e5e4c803cce17ef61e6c6`）
- IFDライブラリ: `px4-userland-ifd.so`（SHA-256: `d52e4c708c4077d3ad5ecc6c5fa349e88253ad4b264a1de4f12134cfc985229a`）
- ファームウェア: IT930xファームウェア（SHA-256: `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`）
- ハードウェア: Latitude 5300
- OS: AnduinOS（Linux 7.0.0-31-generic）
- AppArmorバージョン: 5.0.2
- 使用チューナー: PX-Q3U4 1台（USB ID `0511:084a`、2つのUSB機能）
- 起動方法: 名前付きprofileをロードし、`aa-exec -p` 経由で `px4d` を直接起動（拘束対象はUSBを所有する `px4d`。`pcscd` の実labelは `unconfined`）

## 検証結果

### 2026-09-15 完全再検証結果

- **Deny gate**:
  - 現在の2 USB nodeを `px4d` 自身がopenして拒否され、exit 7、AppArmor拒否ログ2件でpassした。
- **Complain**:
  - 8 receiver、direct APDU、PC/SC reader列挙・ATR・APDUを同一runで同時確認し、予期しない拒否は0件であった。
- **Enforce 60秒**:
  - 8 receiver同時受信、PC/SC APDU 6/6、USB/protocol error 0件、AppArmor拒否0件でpassした。
- **Enforce 30分soak（先行fail 2回、3回目pass）**:
  - 先行1回目（fail）: PC/SC標本30/30成功、AppArmor拒否0件であったが、受信機0/1/4/5でTEI各8が発生し、exit 8となった。
  - 先行2回目（fail）: PC/SC標本30/30成功、USB error 0件、AppArmor拒否0件であったが、受信機0〜7にcontinuity errorが順に7/3/9/4/7/3/9/4発生し、exit 8となった。
  - 再接続後3回目（pass）: 受信機0〜7の全8系統ですべてのsyncエラー、TEI、continuityエラー、キュー破棄、USBエラーが0件、全受信機exit 0で完走した。
    - 受信機0: 28,882,921 packets / 5,429,989,148 bytes
    - 受信機1: 28,871,140 packets / 5,427,774,320 bytes
    - 受信機2: 20,829,536 packets / 3,915,952,768 bytes
    - 受信機3: 20,817,491 packets / 3,913,688,308 bytes
    - 受信機4: 28,818,439 packets / 5,417,866,532 bytes
    - 受信機5: 28,806,351 packets / 5,415,593,988 bytes
    - 受信機6: 20,783,450 packets / 3,907,288,600 bytes
    - 受信機7: 20,770,470 packets / 3,904,848,360 bytes
    - 並行PC/SC APDU 30/30すべてが `90 00` で成功し、`px4ctl status` のUSB/protocol errorは0件、AppArmor拒否は0件であった。
- **受信中物理切断**:
  - 8 receiver同時受信およびPC/SC consumer動作中にQ3U4を物理切断した。
  - 受信機0〜7はすべて exit 7（`DISCONNECTED`）で有限終了し、`px4d` も有限終了した。
  - `pcsc_scan` は reader unavailable および RPC transport error を観測し、切断直後のAPDUは exit 25 を109msで返却した。
  - `pcscd` は35msで明示停止でき、プロセス、ランタイム、profileの残留は0件であった。
- **再接続復帰**:
  - OS再起動なしで新USB node 14/15を同定し、profileを更新・再ロードした。
  - 8 receiver同時受信（60秒）、direct APDU、PC/SC reader列挙・ATR・APDU 6/6すべてが成功し、AppArmor拒否0件で復帰した。
- **クリーンアップ**:
  - 試験profile、試験プロセス、ランタイムエンドポイント、一時IFDおよびreader設定の残留は0件であった。

### 2026-09-14 初回検証結果（履歴）

- 範囲: `px4d` 直接APDUまでの基本機能（IFD / `pcscd` 経路および物理切断は未実施）。
- 30分受信時の測定値:
  - `px4ctl status` 157回中157回成功、直接カードAPDU 157回中157回成功（全応答末尾 `90:00`）。
  - デーモンのファイルディスクリプタ数は29固定、RSSは94,812〜114,820 KiBの範囲で推移（安定推移）。
  - 受信機0〜6は全エラー0件、受信機7は既知バースト基準内（TEI 11,010、continuity 669）。
- 詳細な測定値は [OS・環境別の検証結果](validation-results.md) を参照する。

## profileに必要だった権限

- `px4d` 本体の実行権限。
- ファームウェアの読み取り権限。
- 対象USBデバイスノード（2ノード分）の読み書き権限。
- USB sysfsおよびudevデータベースの読み取り権限。
- netlink通信権限。
- プロセス間シグナル送信権限。
- Unixドメインソケットの作成権限。
- 専用ランタイムディレクトリへの読み書き権限。
- ファームウェアおよびランタイムディレクトリは、実行ユーザーが通常権限でアクセス可能な所有権を設定する（所有権の不一致を `dac_override` や `dac_read_search` で回避する構成は避ける）。

> [!NOTE]
> - 拘束された `px4d` を介して、IFD Handler および `pcscd` consumer 経路が動作することを確認した。
> - `pcscd` 自体の実labelは `unconfined` であり、`pcscd` 専用の AppArmor profile による拘束は検証対象外である。

## USBデバイスノードの扱い

- `/dev/bus/usb/BBB/DDD` のバス番号およびデバイス番号は動的に割り当てられるため、固定パスでの記述を避ける。
- profileの生成および再読み込みの直前に、sysfsの `busnum` および `devnum` からノード番号を取得する。
- 取得時はVID:PIDに加え、2機能それぞれのシリアル番号および物理USBパスを照合する。
- 2ノードが同一のPX-Q3U4であることを確認できない場合は、profileのロードを中断する（fail-closed構成）。
- チューナーを再接続した際は、パスの再照合とprofileの再生成・再読み込みを実施する。

## Deny試験の注意

- USBアクセスの拒否を検証する際は、`px4d` 自身にprofile内でノードを探索・openさせる。
- profile外部で開いたファイルディスクリプタを `--fd` で渡す構成では、`px4d` に対するデバイスノードopen拒否を検証できない。
- ファームウェアの権限不足や親ディレクトリの探索権限不足があるとUSB openより前に終了するため、拒否時は監査ログの対象パスを確認する。
