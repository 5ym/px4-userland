# Debian 12 i386（32-bit / glibc / EHCI）での検証結果

## 検証環境

- 検証対象: `px4-userland` Stable v0.1.3 source archive（SHA-256: `7f86c477cea2d4100112378029dde2be0c68bd4461a1eb2a10cfa13d26c63b50`）
- ホストハードウェア: EPSON Endeavor NJ1000
- OS: Debian 12 i386（kernel `6.1.0-53-686-pae`、32-bit / glibc、EHCIコントローラ）
- 使用チューナー: PX-Q3U4 1台

## 検証結果

- PC/SC IFD有効・必須のnative Release build（98/98 targets）およびCTest（7/7）が成功し、主要成果物（`px4d`、`px4-ts`、`px4ctl`、`libpx4-userland-ifd.so`、`px4_ifd_tests`）はELF 32-bit Intel 80386であった。
- PC/SC smoke試験において、`pcscd`によるreader列挙、ATR取得、およびAPDU送信による応答末尾`90 00`を確認した。
- PX-Q3U4による8受信機同時の60秒gate試験および30分soak試験が合格した。
- 30分連続受信において、受信機0〜6は各約39億バイトを受信し、同期エラー、TEI、continuity error、キュー破棄、USBエラーはすべて0件であった。
- 受信機7のエラー発生状況は、同一試験個体で追跡中の既知バーストを再現した（TEI 10,922、continuity error 620、その他のエラーは0件）。
- 30分連続受信の並行下において、PC/SC経由のATR・APDU取得は30/30標本、直接APDU送信試験は30標本×10回の全試行が成功した。
- ステータス照会におけるUSBエラーおよびプロトコルエラーは全標本で0件であった。
- デーモンのファイルディスクリプタ数は29〜30で安定し、RSSは約122MBで推移した。最高観測温度は56℃であった。
- 8受信機受信中のUSB物理切断試験において、デーモンおよび8クライアントが約2秒で`DISCONNECTED`を検知し、終了コード7（exit 7）で有限終了した。PIDおよびエンドポイントの残留はなかった。
- 切断試験時のsummary不足によるハーネス失敗判定は期待動作であり、プロセスの早期終了とリソース解放を確認した。
- チューナー再接続後の8受信機同時60秒試験において、全受信機が正常終了（exit 0）し、全エラー0件であった。並行して実施したPC/SC ATR・APDU（3/3）および直接APDU（5標本×10回）もすべて成功した。
- 試験終了後に試験用プロセス、エンドポイント、PC/SC設定、ランタイムの残留がないことを確認した。

## 適用範囲と境界

- 検証対象はStable v0.1.3 source archiveからのネイティブビルドである。
- 実機操作はroot/sudo権限で実施した。
- LNB電圧は全試験で0V（給電なし）とした。
- 受信機7のエラー値は同一試験個体における測定記録である。

## 8受信機・PC/SC同時稼働下の物理切断および再接続試験

既存の物理切断試験ではPC/SC consumerを同時稼働させていなかったため、8受信機、デーモン（`px4d`）、`pcscd`、常駐`pcsc_scan`、および反復APDUクライアント（`scriptor`）を同時稼働させた状態での物理切断と、OS再起動なしの再接続復帰を追加検証した。物理切断時に全受信機およびデーモンが有限時間で終了し、PC/SC側も有限時間で切断状態を観測して明示cleanupできた。再接続後もハーネス既定規則によりoverall passとなり、PC/SCの利用再開を確認した。

### 実測事実

- 物理切断試験（8受信機およびPC/SC consumer同時稼働）:
  - 8受信機、`px4d`、`pcscd`、常駐`pcsc_scan`、および反復`scriptor`を同時稼働させた状態でUSBケーブルを物理切断した。切断前の反復APDUは36標本が正常に成功した。
  - USB消失の検出（2026-09-15T02:01:33.450863960Z）から約539 ms後の37標本目APDU（2026-09-15T02:01:33.990158552Z）が終了コード255で失敗した。
  - 常駐`pcsc_scan`はカード状態として `Card state: Status unavailable` を観測した。常駐型のため自発終了はせず、観測後に試験スクリプトからTERMシグナルを送信して終了コード143で明示cleanupした。
  - 切断後の単発`scriptor`は69 msで終了コード255を返却して失敗した。
  - `pcscd` は63 msで終了コード0により正常停止した。
  - `px4d` は `shutdown cleanup: DISCONNECTED` および `px4d stopped: DISCONNECTED` を記録し、終了コード7で有限終了した。
  - 受信機0〜7はすべて `px4-ts: DISCONNECTED` を記録し、終了コード7で有限終了した。切断までに受信したデータ量は以下のとおりであった。
    - 受信機0: 1,070,212 packets / 201,199,856 bytes
    - 受信機1: 1,050,054 packets / 197,410,152 bytes
    - 受信機2: 1,013,239 packets / 190,488,932 bytes
    - 受信機3: 992,173 packets / 186,528,524 bytes
    - 受信機4: 975,346 packets / 183,365,048 bytes
    - 受信機5: 953,625 packets / 179,281,500 bytes
    - 受信機6: 917,379 packets / 172,467,252 bytes
    - 受信機7: 888,741 packets / 167,083,308 bytes
  - 本切断試験ハーネスにおける通常soak用integrity判定はsummary不足によるoverall fail判定となったが、切断を前提としたfault-path試験における期待動作であり、正常soakの合否判定へは転用しない。
  - 切断後においてエンドポイント、試験所有プロセス、およびreader設定の残留は0件であった。
  - カーネルtaint値は切断前後とも0であり、カーネルログにOops、page fault、allocation failure、hung task、および意図しないUSBリセットは記録されなかった。
  - 切断試験完了後、`pcscd.socket`をactive、`pcscd.service`をinactiveとする基準状態へ復元した。

- 再接続復帰試験（OS再起動なしの復帰検証）:
  - OSを再起動せずにチューナーを物理再接続した。USB device numberは24/25から27/28へ遷移し、両機能のシリアル番号は同一であることを確認した。
  - 再接続後の8受信機同時60秒runは終了コード0、`status=completed`、`stop-reason=duration-complete`、受信機7の既定規則（`known-receiver7-burst-nonblocking`）適用を含めてハーネス既定規則でoverall passとなった。
  - 受信機0〜6は同期エラー、TEI、continuity error、キュー破棄、およびUSBエラーのすべてが0件のcleanであった。
  - 受信機7は既定の判定規則に従い `known-receiver7-burst-nonblocking`（793,063 packets / 149,095,844 bytes、同期エラー0、TEI 10,958、continuity error 624、キュー破棄0、USBエラー0、終了コード8）を記録した。過去の実測である全8受信機clean復帰の記録は保持し、本結果は別runとして併記する。
  - 並行して実施した直接APDU送信試験は9標本×各10回の試行がすべて成功した。
  - PC/SC経由のreader列挙、所定ATR（`3B F0 12 00 FF 91 81 B1 7C 45 1F 03 99`）、およびAPDU応答末尾 `90 00` を3/3標本で確認した。
  - 試験完了後もエンドポイントやプロセスの残留は0件であり、カーネルtaint値0およびPC/SCサービス基準状態への復元を確認した。
