# OS・環境別の検証結果

本ドキュメントは特定revisionにおける実測記録であり、将来版やすべての実行環境における動作を保証するものではありません。

## 2026-09-30 PX-M1UR / PX-S1UR 候補版のクロスプラットフォーム・アクセスパス実機試験

CIの**push-run候補** `2f555ff0542c7a36fb2565b64ebc0703f44a0931`（firmware SHA-256: `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`）を用い、
HAOS Studio Code Server（Debian 13 glibc x86_64）、HAOS Supervisor管理Alpine add-on（musl x86_64）、
およびM2 Mac mini（macOS 26.6.2 arm64）で実機試験を実施した。
各pathで使用した候補アーカイブのSHA-256は以下のとおりである（ファイル名の`0.1.7`は候補ビルドのラベルであり公開リリースではない）。
- Linux glibc x86_64: `ae2ac0c3c88dc95a948929784bb2d6813522cf1526d21383a7a3339f1c6da8eb` (`px4-userland-0.1.7-linux-glibc-x86_64.tar.gz`)
- Linux musl x86_64: `e283d88f1a529a2d9aa76043d08a0563e2ba5acd6dc7535b054002d29a95595d` (`px4-userland-0.1.7-linux-musl-x86_64.tar.gz` staged)
- macOS arm64: `333d6cbaa4d5825ba067122c7bbceff6b07e168d5a82fa5ab75c04dfadb3199f` (`px4-userland-0.1.7-darwin-arm64.tar.gz`)

両機種ともUSB serialは`000000000000001`で、PX-M1UR（`0511:0854`、1 receiver ISDB-T/S）、PX-S1UR（`0511:0855`、1 receiver ISDB-T専用）を個別のUSB IDで識別し、1台ずつ接続した。
AndroidおよびWindowsは本試験の対象外である。

| Model / 環境 / access path | 30分連続受信 + PC/SC併走 | 短時間受信・カード・追加確認 | USB切断/再接続・カード抜去/再挿入 |
|---|---|---|---|
| PX-M1UR<br>HAOS SCS<br>Debian 13 glibc x86_64 | ISDB-T 527143 kHz 1800秒。20,666,794 packets / 3,885,357,272 bytes。sync/TEI/continuity/queue-drop/USB errors 0、empty intervals 1,563,574、exit 0。受信中に実PC/SC `scriptor` APDU 290/290成功（全てSW 90 00）。中間statusはready/streaming、errors 0。 | ISDB-T 527143 kHz 10秒（115,834 packets / 21,776,792 bytes）、ISDB-S 1049480 kHz slot 0 LNB 0V 10秒（116,668 packets / 21,933,584 bytes）、全エラー0、exit 0。直接カードstatus/ATR、APDU 10回成功。 | カード抜去時`card-present=no`、generation 1→2、APDUは`NO_CARD`（exit 9）、`pcsc_scan`でCard removed。再挿入後generation 3、直接ATR/reset/APDU 10回、PC/SC reset/APDU成功、再挿入後T 5秒（57,901 packets）エラー0。USB切断時旧daemon生存も`DISCONNECTED`（exit 7）、再接続時USBデバイス番号024で再列挙されても旧daemonは自動復帰せず。旧daemon停止・同一候補daemon新規起動で復帰確認（T 10秒 115,834 packets、S 0V 10秒 160,718 packets、全エラー0）。 |
| PX-S1UR<br>HAOS SCS<br>Debian 13 glibc x86_64 | ISDB-T 527143 kHz 1800秒。20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errors 0、empty intervals 1,568,298、exit 0。受信中に実PC/SC `scriptor` APDU 70/70成功（全てSW 90 00）。中間statusはready/streaming、errors 0。 | 直接カードstatus/ATR、APDU 10回成功。実PC/SC `scriptor`でS1URリーダー選択、resetおよびAPDU SW 90 00成功。同一daemon上でISDB-T 527143 kHz 5秒受信を2回実施し（58,714 packets、57,899 packets、全エラー0、exit 0）、clean stop/reopenを確認。初回の試験コマンドは`--group`フラグ欠落による`INVALID_ARGUMENT`であり、修正後の実行で合格（運用上の指定漏れであり製品不具合ではない）。 | カード抜去時`card-present=no`、generation 1→2、直接`card-atr`は`NO_CARD`（exit 9）、`pcsc_scan`でCard removed。再挿入後generation 3、直接ATR/reset/APDU 10/10（SW 90 00）、実PC/SC reset/APDU（SW 90 00）成功、再挿入後T 5秒（57,898 packets / 10,884,824 bytes）全エラー0。USB切断時旧daemon・`pcscd`生存も`DISCONNECTED`（exit 7）、`pcsc_scan`でCard removed。再接続時USBデバイス番号026で再列挙されても旧daemonは`DISCONNECTED`（exit 7）のまま自動復帰せず。旧daemon停止・同一候補daemon新規起動でready/free復帰、実PC/SC reset/APDU（SW 90 00）および直接APDU 10/10成功。再起動後T 10秒（115,833 packets / 21,776,604 bytes、sync/TEI/continuity/queue-drop/USB errors 0、exit 0）で復旧確認（restart-based recovery）。 |
| PX-M1UR<br>HAOS Supervisor<br>Alpine musl x86_64 | 30分soakは未実施。 | ISDB-T 527143 kHz 約32秒（368,794 packets / 69,333,272 bytes）、ISDB-S 1318000 kHz slot 0 LNB 0V 約32秒（510,788 packets / 96,028,144 bytes）、全エラー0、exit 0。衛星15V要求は`UNSUPPORTED`（exit 3）で拒否。直接カードATR/reset/APDU 10回、PC/SC `opensc-tool` APDU SW 90 00成功。 | 物理ホットプラグ（カード抜去・USB抜差し）は未実施。 |
| PX-S1UR<br>HAOS Supervisor<br>Alpine musl x86_64 | 30分soakは未実施。 | ISDB-T 527143 kHz 約32秒（367,978 packets / 69,179,864 bytes）、全エラー0、exit 0。直接カードATR/reset/APDU 10回、PC/SC `opensc-tool` APDU SW 90 00成功。初回はrunnerがISDB-Tへ衛星専用の`--lnb-voltage 0`を渡して失敗し、修正後の再実行で合格。 | 物理ホットプラグ（カード抜去・USB抜差し）は未実施。 |
| PX-M1UR<br>M2 Mac mini<br>macOS 26.6.2 arm64 | 30分soakは未実施。 | ISDB-T 527143 kHz 10秒（116,651 packets / 21,930,388 bytes）、ISDB-S 1318000 kHz slot 0 LNB 0V 10秒（159,909 packets / 30,062,892 bytes）、全エラー0、exit 0。同一daemon上でISDB-T 527143 kHz 5秒受信を2回実施（各59,531 packets、全エラー0、exit 0）しclean stop/reopenを確認。Homebrew `pcsc-lite`実consumerで13バイトATR、reset、APDU 10/10成功（SW 90 00）。 | カード抜去時`card-present=no`、generation 1→2、直接`card-atr`は`NO_CARD`（exit 9）、Mac PC/SC consumer接続失敗（exit 1）。再挿入後generation 3、直接ATR/reset/APDU 10/10（SW 90 00）、Mac native PC/SC reset/APDU 10/10（SW 90 00）成功、再挿入後T 5秒（58,714 packets / 11,038,232 bytes、全エラー0、exit 0）。USB切断時旧daemonは`DISCONNECTED`となり自動復帰せず、再接続後`px4d --list`で認識、同一候補daemon再起動で復旧確認（先行試験の再起動後T 10秒 116,651 packets、S 10秒 159,909 packets、全エラー0、restart-based recovery有効）。 |
| PX-S1UR<br>M2 Mac mini<br>macOS 26.6.2 arm64 | ISDB-T 527143 kHz 1800秒。20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errors 0、empty intervals 1,439,275、exit 0。受信中に実PC/SC consumer APDU 290/290成功（全てSW 90 00）。中間statusはready/streaming、errors 0。 | ISDB-T 527143 kHz 10秒（116,650 packets / 21,930,200 bytes、全エラー0）。直接カードstatus/ATR、APDU 10回成功。実PC/SC consumerでATR取得、reset、受信中APDU 10/10成功。 | カード抜去時`card-present=no`、generation 1→2、APDUは`NO_CARD`（exit 9）、PC/SC接続失敗（exit 1）。再挿入後generation 3、直接ATR/reset/APDU 10回、PC/SC ATR/reset/APDU 10/10成功、再挿入後T 5秒（57,901 packets）エラー0。USB切断時旧daemon生存も`DISCONNECTED`（exit 7）、PC/SC接続失敗（exit 1）。再接続時新USBインスタンス認識も旧daemonは自動復帰せず。旧daemon停止・同一候補daemon新規起動で復帰確認（再起動後T 10秒 115,835 packets、全エラー0）。 |

### 補足事項・運用上の確認事実

- **USB切断・再接続時の復旧挙動**: 物理切断を行ったnative pathでは、USB切断時に旧daemonプロセスは終了せず`DISCONNECTED`を返し続けた。USB再接続後も旧daemonが同一プロセス内で自動再接続することは観測されず、旧daemonを終了して同一候補版バイナリを再起動することで正常復帰を確認した（restart-based recovery）。Alpine add-onでは物理切断を試しておらず、同一プロセス内での自動再接続も立証していない。
- **未検証項目とサポート主張の扱い**: 本記録はCI push-run候補バイナリによる個別アクセスパスの実機試験結果であり、公開版v0.1.7や最終リリース成果物の認定ではない。SPEC 10.3に基づき、各環境で実際に確認された事実のみを記録し、未試験項目（S1UR Latitudeでの30分PC/SC併走、M1UR macOSでの30分soak、Alpineでの30分soakや物理ホットプラグなど）への推論による`runtime-supported`の昇格は行わず、判定を保留（pending）とする。
- **30分試験の原始出力**: 保存先はHAOSのCodexセッショントランスクリプト `/config/.tools/codex-home/sessions/2026/09/26/rollout-2026-09-26T19-34-40-01a0dd48-080b-7171-8e78-91710ff6cb17.jsonl`。候補版の各`px4-ts`終了出力は、PX-M1UR Latitudeがordinal 19071（2026-09-29 10:43:05 UTC、`empty-intervals=1592300`）、PX-S1UR Latitudeが20543（11:35:05 UTC、`1591871`）、PX-S1UR HAOS SCSが23619（13:48:31 UTC、`1568298`）、PX-S1UR macOSが27377（16:36:12 UTC、`1439275`、`PX4_TS_EXIT=0`）に残る。いずれも`stream packets=20667610 bytes=3885510680`を個別に出力した。独立した実行でパケット数が完全一致した理由は未解明であり、パケット内容が同一または異なることの証拠にはしない。各試験のモデル・host・時刻・候補archiveの対応は上表と同セッション内の起動・状態・PC/SC出力に記録されている。

## 2026-09-29 PX-M1UR / PX-S1UR 候補版のLinux実機試験

Latitude 5300 / AnduinOS 2.0.3（Linux x86_64 / glibc 2.43、native libusb）で、
CIの**push-run候補** `2f555ff0542c7a36fb2565b64ebc0703f44a0931` を試験した。
使用した`px4-userland-0.1.7-linux-glibc-x86_64.tar.gz`のSHA-256は
`ae2ac0c3c88dc95a948929784bb2d6813522cf1526d21383a7a3339f1c6da8eb`、
manifestの`source_ref`は同じcommitである。archive名の`0.1.7`は候補ビルドのラベルであり、
公開済みv0.1.7のバイナリではない。firmwareのSHA-256は
`5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`。
両機種のUSB serialは`000000000000001`で、それぞれ別のUSB IDで識別し、1台ずつ接続した。
以下の結果はこの候補とこのLinux native pathに限る。

| Model / USB ID | 30分連続受信 | 受信・カード・USBの追加確認 | PC/SC併走10分 |
|---|---|---|---|
| PX-M1UR `0511:0854` | receiver 0、ISDB-T 527143 kHz、20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errorsは全て0、exit 0。 | 1 USB / 1 receiver / ISDB-T/Sの列挙、ISDB-T/S 0V受信、同一leaseのT→S→T→S→T、15V要求のopt-inなし・あり双方で`UNSUPPORTED`、カード抜去/再挿入・ATR/reset/APDU、USB切断・再接続後にdaemon新規起動してT/Sとカードの復帰を確認。 | 527143 kHz、6,889,450 packets / 1,295,216,600 bytes。TS/USB errorsは全て0、exit 0。実PC/SC consumer `scriptor`の受信中APDU 60/60成功。 |
| PX-S1UR `0511:0855` | receiver 0、ISDB-T 527143 kHz、20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errorsは全て0、exit 0。 | 1 USB / 1 receiver / ISDB-T専用の列挙、ISDB-S要求の拒否、527143→521143→527143 kHzの同一lease retuneで3区間全てlock・TS errors 0、カード抜去/再挿入・ATR/reset/APDU、USB切断・再接続後にdaemon新規起動してTとカードの復帰を確認。 | 527143 kHz、6,889,450 packets / 1,295,216,600 bytes。TS/USB errorsは全て0、exit 0。実PC/SC consumer `scriptor`の受信中APDU 60/60成功。 |

両機種ともPC/SCのリセットと反復APDUを実consumerから確認した。USB切断後の旧daemonは
アイドル時に`DISCONNECTED`を返し続けたため、停止してから同じ候補版を新規起動した。
同一プロセスでの自動再接続は立証していない。試験後はdaemonを停止し、PC/SCサービスを
試験前の停止状態へ戻し、一時reader設定を退避した。

この記録はPX-M1UR / PX-S1URについて、上記Linux native pathの
`tuner-hardware-verified`、`card-core-hardware-verified`、`native-card-adapter-verified`の
個別証拠である。一方、上記表中のPC/SC併走10分短縮は初期の試験運用であり、
現行SPEC 10.2.7・10.3の30分条件を変更・緩和しない。同日夜の追試において、PX-M1URはLatitude上で
ISDB-S 0V（1318000 kHz、slot 0）の1800秒連続受信とnative PC/SC併走（`scriptor` APDU 10×5=50/50成功、
28,619,541 packets / 5,380,473,708 bytes、TS/USB errors 0）を完走した。
一方、PX-S1URのLatitude環境自体は依然として10分native PC/SC併走（6,889,450 packets、APDU 60/60成功）に
とどまる（後続試験として別pathのHAOS SCS上でISDB-T 1800秒+native PC/SC 70/70完走を記録したが、
Latitude native pathの代替とはならない）。
したがって「両機種ともPC/SC併走が10分帯までしか観測されていない」という初期の記述は不正確であり、
M1UR（Latitudeでの衛星0V）およびS1UR（HAOS SCSでの地上波）で30分併走を確認済みである。
ただし、S1URのLatitude native path単体では30分PC/SC併走を満たしておらず、M1URのLatitude地上波におけるPC/SC併走も
10分にとどまるため（地上波30分はdirect IPCのみ）、この段階では`runtime-supported`の証拠としない。
Windows / WebTS.app、Androidその他の未試験runtimeへ外挿しない。macOSでの直接の試験結果は前掲の別pathの行に限る。候補版全体のrelease canaryとPR mergeも別gateである。

## 2026-09-29 PX-M1UR 認定途中（v0.1.7）

Latitude 5300 / AnduinOS 2.0.3（Linux x86_64 / glibc 2.43）で、PX-M1UR `0511:0854` を
v0.1.7の正式Linux glibc x86_64 archive（source commit
`b7685ad9940e278bdb0809dec4cc92247b8844ec`、archive SHA-256
`b137938e778b2dccc0b9a1f1ede14040bbc94826d187e5a424f740c4d9ca2acd`）から実測した。
この記録は認定完了や、他OS・他architectureへのサポート主張ではない。

- receiver 0のISDB-T 527143 kHzを正式`px4-ts`で単一の連続1800秒取得。20,667,610 packets、
  3,885,510,680 bytes、sync/TEI/continuity/queue-drop/USB errorsはすべて0。取得中のdirect APDUも成功した。
  これ以前の別の5分試行ではcontinuity errorが1件あり、原因未解明の失敗として保持する。
- ISDB-TとISDB-S（1318000 kHz、slot 0、LNB 0V）の短時間取得、同一leaseのT→S→T→S→T、
  card抜去・再挿入、T/S取得中のnative PC/SCによるATR・APDU・resetを確認した。
- 壁設備から分離した開放端で、`px4d --allow-lnb-power`と`px4-ts --lnb-voltage 15`を指定しても
  30秒間0Vのままであった。計器は別途乾電池で1.5Vを示した。無信号のためtune自体はtimeoutした。
  この測定はPX-M1URのLNB 15V出力を立証しない。参照ドライバでもM1URの給電callbackは無効であり、
  SPEC v0.22では15V出力を対応範囲から除外した。
- 現行v0.1.7コードは、opt-in時にPX-M1URの15V要求を拒否せずGPIO 11を書き込むためSPEC v0.22と
  不一致である。`--allow-lnb-power`あり・なし双方の否定系テストと修正後の実機確認、USB物理切断・再接続、
  最終artifactのcanaryを残す。これらが完了するまでPX-M1URはhardware-verifiedとしない。

参照ドライバ: [Linux M1UR source](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/driver/m1ur_device.c)、
[WinUSB M1UR source](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/winusb/src/DriverHost_PX4/isdb2056_device.cpp)。

同じ参照revisionのLinux driverでは、DTV02-1T1S-U / DTV02A-1T1S-Uに対応する
[ISDB2056 / ISDB2056N](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/driver/isdb2056_device.c)も
LNB setterが無効で、GPIO 11初期化は無効化されている。
[S1UR / ISDBT2071](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/driver/s1ur_device.c)は
地上波専用でGPIO 11初期化経路を持たない。これらは参照実装との一致を示すだけで、手元にない機種の
実機電圧・受信動作を確認したことにはならない。

## 2026-09-05 共通回帰

| 環境 | arch/libc | revision | 確認内容 |
| --- | --- | --- | --- |
| Home Assistant OS | x86_64 / Alpine musl userspace | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。地上波と衛星を各3 cycle、30秒sampleで確認。両bridge、8 receiver、終了時socket cleanupを確認。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。後続試験で内蔵カードリーダーも確認。 |
| Latitude 5300 / AnduinOS | x86_64 / glibc 2.43 | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。native Release build、CTest 7/7、地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。試験時はUSB node権限のためhardware操作のみsudoを使用。 |
| M2 Mac mini / macOS 26.6.2 | arm64 | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。後続試験でPC/SC IFD、内蔵カード、物理切断と再接続を確認。 |
| Pixel 9a / Android API 37 / Termux | aarch64 / Bionic | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。UsbManagerから得た2 fdで地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。 |
| Google TV Streamer / Android API 34 / Termux | armeabi-v7a / Bionic | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。2 fdで地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。 |
| Google TV Streamer / ad-hoc APK | armeabi-v7a / Bionic | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。Android USB Host API所有の2 fdで地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。後続試験で内蔵カードのdirect IPCを確認。APKはこのリポジトリの配布物ではない。 |

## Linuxディストリビューション追加検証

| 環境 | revision | 確認内容 | 補足 |
| --- | --- | --- | --- |
| HAOS上のDebian 13 Studio Code Server container（x86_64 / glibc 2.41） | — | PX-Q3U4を使用。static CLIのsmoke、soak、物理切断と再接続を確認。 | container root実行であり、一般ユーザー権限試験の代用ではない。 |
| HAOS Supervisor管理Alpine add-on（x86_64 / musl） | — | PX-Q3U4を使用。SupervisorのUSB公開、container起動停止、`px4d`とPC/SC consumerのcleanup順、物理切断時の有限終了、再接続後の手動復帰、自動restart loopなしを確認。 | — |
| Alpine Linux 3.24.1（x86_64 / musl 1.2.6） | `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` | PX-Q3U4を使用。一般ユーザー、両USB bridge、8 receiver同時、direct APDU、musl IFD、PC/SC、終了後cleanupを確認。 | [Alpine Linuxの構成例](alpine-mdev.md) |
| Fedora Linux 42（aarch64 / glibc 2.41 / kernel 4.9.140-l4t+） | `df6a1e634e5bec11961a1f0f15eedd1da22f7fee` | PX-Q3U4を使用。配布archiveを一般ユーザーで実行。8 receiver同時30秒でreceiver 0〜6はclean、receiver 7は既知burstのみ、USB/protocol error 0、direct APDU成功、glibc aarch64 IFDとPC/SCでATR/APDU成功、process/endpoint残留なしを確認。 | SELinuxはDisabled。 |
| NixOS 26.05.9227.c25784012c99（x86_64 / glibc 2.42） | `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` | PX-Q3U4を使用。一般ユーザー、8 receiver同時、direct APDU、glibc IFD、PC/SCを確認。 | [NixOSの構成例](nixos.md) |
| Chimera Linux rootfs snapshot 20251220（x86_64 / musl / libc++） | `3e6e32a107566689a9b4bf223dca1e4e89f67cfa` | PX-Q3U4を使用。Clang 22 native build、CTest 7/7、8 receiver同時、direct APDU 100/100、native IFD、一般ユーザーPC/SCを確認。 | [Chimera Linuxの構成例](chimera-linux.md) |
| Fedora 44（x86_64 / glibc / SELinux Enforcing） | `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` | PX-Q3U4を使用。native Release build、CTest 7/7、systemd自動起動、専用SELinux domain、一般ユーザーIPC、PC/SC、地上波・衛星、direct APDU 10/10、OS再起動後回帰、AVC拒否0を確認。 | [Fedoraの構成手順](../../packaging/fedora/README.md) |
| FreeBSD 15.1-RELEASE（amd64） | — | PX-Q3U4を使用。native build、CTest 5/5、地上波と衛星の同時受信、内蔵カードAPDU 10/10を確認。 | 現行Release対象外。 |
| OpenWrt 25.12.5（x86_64 / musl 1.2.5 / procd） | `29635988c5692eb9167dc082ef0b4c4e7dfb5e04`と同内容 | PX-Q3U4を使用。static/stripped成果物、地上波と衛星の同時受信、APDU 10/10、procd起動停止、物理切断時exit 7・process/socket残留なし、再接続後復帰を確認。 | OpenWrt向けソース修正なし。 |
| Gentoo Linux 2.18（x86_64 / glibc 2.43、kernel 6.18.48-gentoo-dist-bin、GCC 15.3.0、OpenRC） | `fa45792787905d3a86a8cab0bbc9ab7c860c665e`後の未commit差分 | PX-Q3U4を使用。native build（PCSCなし 92/92 targets・CTest 5/5、PCSCあり 98/98 targets・CTest 7/7 PASS）。一般ユーザーでの直接カードAPDU 10/10、地上波・衛星単系統smoke、標準OpenRC `pcscd`経由のreader列挙・ATR・APDU SW9000を確認。8 receiver同時30秒+APDU 10/10はreceiver 0〜6が全エラー0、receiver 7のみ既知個体burst（TEI 10,935、continuity 652、queue/USB error 0、rc8 `PROTOCOL_ERROR`）。receiver 6・7の各30秒単独比較でもreceiver 6は全エラー0、receiver 7のみTEI 10,920・continuity 594を再現。daemon rc0、終了時のprocess/socket残留なしを確認。 | Gentoo/OpenRC向けソース修正なし。一般ユーザーはusbグループ所属でUSBノード（root:usb 0664）をsudoなしで利用可能。pcsc-lite 2.4.1（pcscd:pcscd実行、reader設定dirをpcscd所有に設定）、pcsc-tools 1.7.4を使用。 |
| EPSON Endeavor NJ1000 / Debian 12 i386（32-bit / glibc / EHCI） | Stable v0.1.3 source archive | 合格。PC/SC IFD込みnative Release build（98/98 targets）、CTest 7/7、PC/SC smoke、8受信機同時30分soak（r7既知バースト再現、PC/SC・direct APDU完走）。8受信機とPC/SC consumer同時稼働下の物理切断（全受信機・px4d有限終了 exit 7、PC/SC切断観測および明示cleanup、残留0）、およびOS再起動なしの再接続（60秒runでr0〜r6 clean、r7既知バーストexit 8を含めハーネス既定規則でoverall pass、PC/SC・direct APDU成功、過去runの全8 clean復帰も併記保持）を確認。 | 実機操作はroot/sudoで実施。詳細は [Debian 12 i386での検証結果](debian-i686.md) を参照。 |
| Latitude 5300 / AnduinOS（localhost usbip / VHCI） | Stable v0.1.3 x86_64 glibc archive | 合格（初回strict fail保持、再試BS15_0完走）。VHCI側2ノードを`--fd`指定し8受信機30分soak、direct APDU完走。 | localhost/VHCI経路の実績。LAN経由は再検証対象。詳細は [localhost usbipでの検証結果](usbip-localhost.md) を参照。 |
| Latitude 5300 / AnduinOS（AppArmor enforce） | Stable v0.1.3 x86_64 glibc archive | 合格。USB拒否gate、complain、enforce 60秒、enforce 30分（先行fail 2回を経て再接続後3回目で8 receiver全エラー0完走）、拘束`px4d`経由でのIFD/`pcscd` consumer動作（APDU 30/30 SW9000）、受信中物理切断（全receiver exit 7、有限停止）、OS再起動なしの再接続復帰（8 receiver 60秒・PC/SC APDU 6/6 pass）、cleanup passを確認。 | profileは利用者側で用意する。`pcscd`の実labelはunconfinedであり、`pcscd`専用profileは検証対象外。詳細は [AppArmorで実行する際の注意](apparmor.md) を参照。 |

## 2026-09-10 Android正式launcher実機検証（レビュー前候補archive）

対象branch headは`fa45792787905d3a86a8cab0bbc9ab7c860c665e`。以下は、40秒猶予およびbytecode監査の修正前に作成した、
正式launcher実機検証済みのレビュー前候補archiveによる実機結果であり、現在の最終候補archiveではない。

| 環境 | レビュー前候補archive SHA-256 | 確認内容 |
| --- | --- | --- |
| Pixel 9a（Android 17 / aarch64 / Bionic、Termux 0.118.3） | `737f2b42d9b02be9763ad186dee8129a17121fbc00c753d4e8e3bdf9516b6723` | 正式`px4-termux`で内蔵カード、地上波、衛星、TERM/INT/HUP、第2USB取得失敗、30分8 receiver、APDU 30/30、物理切断exit 7を確認。receiver 0〜6は全エラー0、receiver 7は既知burstのみ。全processとIPC endpointの残留なし、再接続後も正常。 |
| Google TV Streamer（Android 14 / API 34 / armv7a / Bionic、Termux 0.119.0-beta.3） | `d7da07acea5f676f698a86e92c0b80e87c1dbe2aca8b01414cc5db8cd24f302b` | 正式`px4-termux`で内蔵カード、地上波、衛星、TERM/INT/HUP、第2USB取得失敗、30分8 receiver、APDU 30/30、物理切断exit 7を確認。receiver 0〜6は全エラー0、receiver 7は既知burst（TEI 10,949、continuity 627、queue/USB error 0）のみ。全processとIPC endpointの残留なし、再接続後も正常。 |
| IP3 GT1 / Bliss OS（Android 13 / x86_64 / Bionic、Termux 0.118.3） | `7561b51c0b6e934b044982eac0539b5a3de556c331e59b9c6b6f062d3b8f9222` | 正式`px4-termux`で30分8 receiverを実施し、8/8 exit 0、sync/TEI/continuity/queue/USB errorを全て0で確認。内蔵カードAPDU 30/30、TERM/INT/HUP、第2USB取得失敗、物理切断exit 7、全processとIPC endpointの残留なし、再接続後のカードAPDU 10/10・地上波・衛星を確認。 |

Bliss OSではバックグラウンド時にTermux UID全体が凍結し、`termux-wake-lock`も同環境で`Bad system call`となった。検証中だけADBで給電中の画面常時点灯とTermux前面表示を使用し、`stay_on_while_plugged_in`は元の`0`へ復元した。最初の凍結したsoakは無効試験として上記合格値に含めていない。

## 2026-09-11 Stable基準候補実機検証（commit eabb60b / CI run 34495151505）

対象branchは`fix/macos-system-pcsc`、commit `eabb60bf7702654de20e4f347cfb6c0280ededef`、VERSION `0.1.2`、GitHub Actions run `34495151505`（17/17 jobs成功）。8 binary archive + source archive（計9 archive）および `SHA256SUMS` を生成し、チェックサム検証・展開監査済み。本候補のexact archiveを用いた各対象環境の実機検証ゲートを完了（main統合、version bump、tag、Stable Releaseはこの検証実施時点では未実施）。

| 環境 | 使用アーカイブ SHA-256 | 確認内容 |
| --- | --- | --- |
| SCS native Debian 13（x86_64 / glibc） | `fed63ce6f1e9e16ed2b56eed337c33b43bfa8906169f4fd54ed71d9b787588b5` | 2時間ソーク（300秒×24サイクル、24/24 pass）。8 receiver再取得・再チューニング・停止、内蔵カードAPDU監視120/120成功、daemon FD 29・RSS安定、終了時process/IPC残留なしを確認。receiver 192件中191件clean、receiver 7既知burst 1件（sync/queue-drop/USB error 0）。 |
| HAOS Supervisor管理Alpine add-on（x86_64 / musl） | `e77305ead8160ba48dac500813abf04c070a0b60e28dda65184f825e7ef7725e` | 2時間ソーク（24/24サイクルpass）。初回試験での地デジ過渡異常（fail扱い）を経て新規24周連続取得で完走。receiver 192件全clean（全TSエラー0）、status監視144/144、カードAPDU 144/144成功、daemon FD 31・RSS安定、終了時process/IPC残留なしを確認。 |
| Latitude 5300 / AnduinOS（x86_64 / glibc） | `fed63ce6f1e9e16ed2b56eed337c33b43bfa8906169f4fd54ed71d9b787588b5` | 30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）、status 30/30、カードAPDU 30/30成功。stop/reopen 15秒回帰pass。受信中USB物理切断時のexit 7有限終了、OS再起動なし再接続後の15秒復帰回帰（8/8 clean、APDU成功）、終了時process/IPC残留なしを確認。 |
| M2 Mac mini / macOS 26.6.2（arm64） | `dfde6006311c49070d340d13fba76de76343e1c0d7e7ddb981004ffb36df7655` | 30分8 receiver同時ソーク。初回終了時continuity異常（fail扱い、再現せず不採用）を経て2回目30分ソークpass（receiver 0〜6全エラー0、receiver 7既知burstのみ、APDU 30/30成功）。stop/reopen pass。受信中USB物理切断時の有限終了（exit 7）、再接続後15秒復帰回帰（全TSエラー0、APDU成功）、終了時process/IPC残留なしを確認。 |
| Pixel 9a（Android 17 / aarch64 / Bionic、Termux 0.118.3） | `378f521df009948f305c0ff90dbe34687f03e68739be554687def79096a7974a` | 正式`px4-termux`で30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）。status 56/56、カードAPDU 56/56成功。SIGINT/SIGHUP/SIGTERM/不正第2USB pathでの残留なし。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、カード成功）、終了時process/IPC残留なしを確認。 |
| Google TV Streamer（Android 14 / API 34 / armv7a / Bionic、Termux 0.119.0-beta.3） | `01d5fd39c36928676956c7df810937c76048d487b600f65c0798d0f82c08c183` | 正式`px4-termux`で30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）。status 57/57、カードAPDU 57/57成功。SIGINT/SIGHUP/SIGTERM/不正第2USB pathでの残留なし。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、カード成功）、終了時process/IPC残留なしを確認。 |
| IP3 GT1 / Bliss OS（Android 13 / x86_64 / Bionic、Termux 0.118.3） | `659c50d4f6461dea7109154007822b3df5d843653164d5b47da33b5381fa5564` | 正式`px4-termux`で30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）。status 58/58、カードAPDU 58/58成功。SIGINT/SIGHUP/SIGTERM/不正第2USB pathでの残留なし。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、カード成功）、終了時process/IPC残留なしを確認。 |
| Google TV Streamer / ad-hoc APK（armv7a / Bionic） | （内部試験器具・非配布） | armv7a archiveと同SHA-256のELF payloadおよびForeground Service修正版APKを使用。Activity background状態で30分8 receiver同時ソーク（8/8 exit 0、全TSエラー0、status 31/31、APDU 31/31成功）。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、APDU成功）、終了時process/IPC残留なしを確認。 |

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。
- この候補（commit `eabb60bf7702654de20e4f347cfb6c0280ededef`）において、Linux aarch64のバイナリは旧候補（commit `df6a1e634e5bec11961a1f0f15eedd1da22f7fee`）とbyte-identicalではなく、glibc / musl ともにこの候補での実機物理試験（チューナー・カード・IFD）は未実施です。SPEC 10.3に従い `build-tested / hardware-unverified` として扱います。

## 既知の観測事項

- 2時間の8 receiver soakではreceiver 0〜6はtransport error 0。receiver 7でTEI 10,974、continuity error 453、sync/queue-drop/USB error 0。同じ約11k TEIの署名は同一個体の参照カーネルドライバ（`tsukumijima/px4_drv`）でも同一周波数（T22）で再現しており、本ドライバ固有の回帰ではなく個体固有の既知事象として記録しています（原因および他個体での挙動は未確認）。
- FreeBSDでは接続直後に片bridgeのfirmware version queryが1回TIMEOUTする事象を2回観測。再試行後は正常。
- Windowsは本プロダクトのサポート外。
