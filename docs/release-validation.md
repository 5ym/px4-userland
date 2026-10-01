# Stable リリース前検証手順

この文書は Stable リリースごとの実行順と、変更内容に応じた追加検証の選び方を定める。合否条件と認定条件の正本は [`SPEC.md`](../SPEC.md) 10章であり、両者に差があれば SPEC を優先し、手順書を更新する。全 OS・全機種を毎回回帰する手順ではない。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 0. 環境IDと手順の共通部品

SPEC 10.5 の canary・long soak は、次の canonical 環境ID E01–E17 で選択する。AppArmor、usbip、SELinux などの変化は独立した環境IDにせず、その環境の access path note として記録する。各環境の product-specific な手順と状態は §6 に示す。

| ID | 環境 | px4 の位置づけ | 主 artifact / doc |
|---|---|---|---|
| E01 | HAOS x86_64 Debian/glibc SCS | canonical runtime（`linux-glibc-x86_64`、container root） | §6 / [validation-results.md](platforms/validation-results.md) |
| E02 | HAOS x86_64 Alpine/musl add-on | canonical runtime（`linux-musl-x86_64`、Supervisor add-on） | §6 / validation-results.md |
| E03 | AnduinOS x86_64 | canonical Linux x86_64 runtime | §6 / validation-results.md |
| E04 | macOS arm64 | canonical runtime（`darwin-arm64`） | §6 / validation-results.md |
| E05 | Android Termux aarch64 | canonical runtime（`android-aarch64`、2-FD） | §6 / validation-results.md |
| E06 | Android Termux armv7a | canonical runtime（`android-armv7a`、2-FD） | §6 / validation-results.md |
| E07 | Bliss OS x86_64 Termux | canonical runtime（`android-x86_64`、2-FD） | §6 / validation-results.md |
| E08 | Fedora x86_64（SELinux） | historical-only / conditional（`packaging/fedora/**` 変更時） | §6 / [Fedora手順](../packaging/fedora/README.md) |
| E09 | Gentoo x86_64 | historical-only / conditional | §6 / validation-results.md |
| E10 | NixOS x86_64 | historical-only / conditional | §6 / [NixOS](platforms/nixos.md) |
| E11 | Chimera Linux x86_64 | historical-only / conditional | §6 / [Chimera](platforms/chimera-linux.md) |
| E12 | Alpine x86_64（mdev） | historical-only / conditional（`packaging/mdev/**` 変更時） | §6 / [mdev](platforms/alpine-mdev.md) |
| E13 | OpenWrt x86_64 | historical-only / conditional | §6 / validation-results.md |
| E14 | FreeBSD x86_64 | 対象外（SPEC §1・§2の対象外。記録は履歴のみ） | 対象外 |
| E15 | Fedora aarch64 | unverified（`build-tested / hardware-unverified`）。claim変更時のみ | §6 / validation-results.md |
| E16 | Debian x86/i386 | source-build-only（i386配布artifactなし） | §6 / [Debian i386](platforms/debian-i686.md) |
| E17 | Windows 11 x86_64 | 対象外（別製品 `tsukumijima/px4_drv`） | 対象外 |
| — | Android ad-hoc APK | 対象外（dtv-android 所管。本リポジトリの gate に含めない） | 対象外 |

用語:

- **canonical runtime**: その artifact の実機検証を代表する環境。同一 executable を共有する runtime（例: E01/E02/E03 の Linux x86_64）でも、access path が違えば証拠は自動継承しない。
- **conditional**: その環境に固有の材料（配布設定、構成手順）を変更したときだけ実行する。
- **source-build-only**: 配布 artifact を持たず、source archive の native build だけで記録する。
- **historical-only**: 過去の記録だけがあり、current claim ではない。
- **unverified**: build/CI は通るが実機 claim がない。

### 0.1 共通コマンド

`<version>`、`<platform>`、`$ID`、`$FW`、`$RT`、`$LOG` は実値に置き換える。artifact は候補 commit の `release-candidate`（§2）だけを使い、手元 build や旧 archive で代用しない。

artifact 検証と展開（X-PREP）:

```sh
sha256sum -c SHA256SUMS                              # macOS: shasum -a 256 -c
D=$(mktemp -d); tar -xzf px4-userland-<version>-<platform>.tar.gz -C "$D"
sha256sum "$FW"                                      # 5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484
RT=$(mktemp -d "${TMPDIR:-/tmp}/px4-userland.XXXXXX"); LOG=<log dir>
"$D/px4d" --list > "$LOG/list.txt"                   # model / usb / status=ready / receivers
```

daemon 起動と準備完了確認（X-START）:

```sh
"$D/px4d" --device "$ID" --firmware "$FW" --runtime-dir "$RT" [--group] 2> "$LOG/px4d.err" &
P=$!; i=0
until "$D/px4ctl" --device "$ID" --runtime-dir "$RT" [--group] status >/dev/null 2>&1; do
  i=$((i+1)); [ "$i" -ge 30 ] && exit 1; kill -0 "$P" || exit 1; sleep 1; done
"$D/px4ctl" --device "$ID" --runtime-dir "$RT" list > "$LOG/ctl-list.txt"
```

受信と監視（X-LOAD / X-MON）:

```sh
# Q3U4 8 receiver（受信継続: 0/1/4/5=S、2/3/6/7=T）
"$D/px4-ts" --device "$ID" --receiver $r --system isdb-s --frequency-khz 1318000 --slot 0 \
  --runtime-dir "$RT" --output /dev/null --duration-seconds $S 2> "$LOG/r$r.err" &
"$D/px4-ts" --device "$ID" --receiver $r --system isdb-t --frequency-khz 527143 \
  --runtime-dir "$RT" --output /dev/null --duration-seconds $S 2> "$LOG/r$r.err" &
# 受信中の監視: px4ctl status（30秒ごと）、px4ctl card-apdu <HEX> --repeat 10（60秒ごと）、FD/RSS（60秒ごと）
```

Termux での起動（X-START-TERMUX）:

```sh
# Termux 2-FD（E05–E07）
RT="$PREFIX/tmp/p4"; mkdir -p "$RT"; chmod 700 "$RT"
"$D/px4-termux" --usb-device <path1> --usb-device <path2> --device "$ID" --firmware "$FW" --runtime-dir "$RT"
# 別セッションで §0.1 の px4d 準備完了 loop と px4-ts / px4ctl を同じ $ID と $RT で実行する
```

終了と残留確認（X-STOP / X-RESID）:

```sh
kill -TERM $P; wait $P           # 0 を期待
pgrep -fl '[p]x4d'; rmdir "$RT"   # 残存 process / endpoint なし
```

Q3U4 preflight: 両 USB device、15V adapter、両 RF lead を接続し、`px4d --list` が `status=ready receivers=8` を返すことを確認してから開始する。

### 0.2 same-lease retune（条件付き）

canary の同一 lease retune は、IPC/lease/retune、frontend/tune、device identity の変更時にだけ行う。それ以外は CI の offline 試験成功を引用する。hardware で行う場合は、既存の out-of-repo `/config/.work/px4-m1ur-s1ur/retune-tool/retune_tool.cpp` を使ってよい。その場合は source と binary の SHA-256、および build 条件（compiler、flags、libusb）を release record に記録する。この tool が利用できないときに trigger が成立した場合、当該 criterion を明示的に block し、pass 扱いしない。新しい tool をこのリポジトリへ追加しない。

### 0.3 receiver 7 参照比較（X-R7REF, 条件付き）

receiver 7 で TEI/continuity burst が出て、SPEC 10.2.6a の比較条件（同一個体・antenna・power・firmware・frequency・duration・同等 capture）を満たす fresh 参照が必要な場合にだけ行う。kernel module を load できる環境（E03 が canonical、E08–E12、E16 も可）で `tsukumijima/px4_drv` を一時 load し、同じ条件で counter を取得した後 unload する。module の load/unload はユーザー確認のうえ実施する。比較条件を再現できない場合は判定保留とし、pass 扱いしない。


## 1. 候補と前回証拠を固定する

1. 候補の `VERSION`、GitHub 上の source commit、candidate workflow run を特定する。dirty な作業ツリーや未 push のローカル変更を候補として試験しない。
2. 前回 Stable 以降の累積差分と、今回使う baseline evidence を確認する。差分をファイル名だけで判断せず、SPEC 10.5.1 の変更分類と実際の呼出経路・依存先から model/profile・runtime/access path・feature の影響範囲を決める。
3. 対象 claim ごとに `継承`、`今回再検証`、`未認定`、`対象外` のいずれかを記録し、根拠を書く。証拠の軸は SPEC 10.2.8 に従う。別 model、別 OS/runtime、別 access path、別 feature へ結果を外挿しない。同一 model・同一 runtime/access path・同一 feature の証拠は、対象 artifact の bytes が baseline と同一であるか、変更がその path へ影響しないと 10.5.1 の表で判定できる場合に継承できる。未知の影響は affected として扱い、`対象外` は SPEC の対象外または本手順 §0 の対象外に限る。
4. 前回の適格な long soak の日付や release 数は判定に使わない。時間経過・release 回数だけを理由にした再認定 gate は設けない。

Termuxのarchitecture、Termux launcherのFD path、glibc/musl、native PC/SC adapterは別々のruntime/access pathとして扱う。WindowsとFreeBSD、Android ad-hoc APKは対象外である。変更も新規claimもないpathを毎回試験しない。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得る最小の path 集合を targeted 再検証する。全 matrix を埋める方向へ拡張しない。

## 2. source / CI / candidate artifact gate（毎回）

1. 候補 commit に対応する `portable userland foundation` workflow を確認する。path filter 等により自動実行されていなければ、GitHub Actions の `workflow_dispatch` でその候補 ref を指定して実行する。
2. workflow 内の unit/offline test、各 target build、source/relink、`release-candidate` と、それに依存する4つの Ubuntu/Alpine × x86_64/aarch64 artifact smoke job がすべて成功していることを確認する。失敗 job を無視して先へ進まない。
3. `release-candidate` artifact が候補 commit の `source_ref` を示し、8 binary archive、対応 source archive、外側 `SHA256SUMS` の計9 archiveを含むことを確認する。チェックサムを照合し、CIの manifest・license・corresponding-source・binary/source archive audit が通っていることを確認する。
4. 候補 commit が変わった場合は、その commit の CI と artifact を取り直す。前の commit の archive、手元で別途作った build、PR run の古い artifactを final candidate の代用にしない。

手動dispatchとartifact取得には次の短いCLI手順を使える。`<candidate-ref>`、`<run-id>`、`<new-empty-dir>`を実際の値へ置き換え、artifactは新しい空ディレクトリへ展開する。既に成功済みの自動runが候補commitと一致するなら再dispatchしない。

```sh
gh workflow run build_userland.yml --ref <candidate-ref>
gh run list --workflow build_userland.yml --limit 10
gh run view <run-id> --json headSha,conclusion
gh run download <run-id> --name release-candidate --dir <new-empty-dir>
(cd <new-empty-dir> && sha256sum -c SHA256SUMS)
```

CI が行う build・audit・smoke を成功後に同じ目的でローカル再実行しない。CIで失敗または未実施の項目がある場合に限り、原因調査に必要な既存コマンドを実行する。新しい汎用検証スクリプトは作らない。

SPEC 10.5.2の「再現性確認」は具体的な受入条件が未定義である。現行 workflow で確認できるのは候補のsource reference、manifest/checksum、archive audit、source/relink jobであり、最終9 archiveを独立したclean buildで再生成してbyte比較する試験ではない。この手順で全platformの独立再buildを追加要求しない一方、記録上もそれを実施済みと表現しない。SPECの再現性gateが独立再buildを意味する場合、対象範囲と判定条件をSPEC/CIへ定めるまで、その要件は未定義として扱う。

## 3. exact-candidate hardware canary（毎回）

1. §2で固定した final candidate artifactそのものを使う。対象 archive名と SHA-256、source commit、model/USB ID、host OS/version、runtime/libc、access pathを記録する。
2. canary は release ごとに1回、10分以上行う。実施環境は次の順で選ぶ。
   - 影響する release artifact がない場合（docs/license/package metadata のみ、または対象 artifact が baseline と byte-identical）: **E03** で PX-Q3U4 の8 receiver ISDB-T/S 混在 canary を行う。
   - 影響する artifact がある場合: 最も複雑な影響 topology（PX-Q3U4 > PX-M1UR > PX-S1UR）を選び、該当する OS/access path の環境に絞り、**E03、E01、E02、E04、E05、E06、E07** の順で最初のものを用いる。
   - 単一 receiver の変更では PX-M1UR を代表とし、S1UR 固有コードの変更時だけ S1UR を加える。受信・カード・給電を伴う canary は両者を順次接続して行う。同時接続での識別確認は §4 の範囲に限る。
3. 試験コマンドは §0.1 と README のCLI仕様に合わせる。選んだ profile に該当する `list/grouping`、ISDB-T/S または plain-TS、status、stop/reopen、搭載時の card/APDU、正常終了を確認する。10分の受信中は SPEC 10.2/10.2.7 の該当 TS条件を満たすことを確認し、packet・byte・counter・終了statusを保存する。profileにない機能や物理構成は試さず、非該当理由を記録する。
4. 同一 lease retune は §0.2 に従い、IPC/lease/retune、frontend/tune、device identity の変更時にだけ canary へ含める。それ以外は offline CI（`userland/tests/tuner_service_tests.cpp`、`userland/tests/control_integration_tests.cpp`）の成功を release record に引用する。
5. 終了後に daemon/client、FD、IPC socket/control endpoint等の残留がないことを確認する。旧candidateや異なるartifactでの結果を今回の canary に数えない。
6. PX-Q3U4 receiver 7 の TEI/continuity burst は自動合格にしない。参照試験がSPEC 10.2.6aの同一個体・antenna・power・firmware・frequency・duration・同等capture条件を満たし、受入条件を満たした場合だけ既知制限として扱う。条件を再現できなければ判定保留とする。

この canary は release gate であり、それだけで全機種・全 runtime/access path の hardware claim を更新しない。

## 4. 変更影響に応じた targeted qualification と long soak

SPEC 10.5.1の分類を基準に、以下の該当行だけを実施する。複数条件に該当する場合は同じ run が全条件を満たせば重複実施せず、結果を各 trigger に対応付けて記録する。

| 判定 | 必要な追加検証 | 要求しない検証 |
|---|---|---|
| docs / license text / package metadata のみ | 各 Stable 共通のCI・archive/license/source gateと10分canary | 新しい hardware requalification・long soak |
| 新規または未認定の追加 profile。Q3U4非影響を立証 | exact candidateで対象 profile ごとに canonical Linux x86_64のSPEC 10.2.7認定を完了し、30分以上連続受信する。影響する別runtime/access pathは個別に targeted 確認する。単一receiverは共通のT/S/cardを持つPX-M1URを代表とし、S1UR固有コード変更時はS1URも対象にする。 | Q3U4の2時間soak。30分profile認定をQ3U4 soakの代用・同等物と呼ばない |
| 共通実装を変更したがQ3U4非影響を立証 | 差分、Q3U4のcall path、条件分岐等による非適用根拠を記録する。加えて exact candidate でQ3U4の8 receiver ISDB-T/S混在受信を、SCS native/glibc と HAOS Alpine/musl の各pathで10分以上行う。status・card APDUを併走し、stop/reopen、TS/USB counter、終了後残留を確認する。 | 上記の証拠が揃い pass なら、変更起因のQ3U4 2時間soak |
| stream/queue/demux/concurrency/lifetime/hotplug/card/power/LNB/USB transport/IPC lease等がQ3U4の長時間挙動に影響し得る、または非影響を立証する短時間回帰が未実施/失敗/曖昧 | exact candidateでSPEC 10.5.2に従うQ3U4 2時間以上の8 receiver T/S混在負荷soak。cycle構造は300秒×24 cycleを用い、cycle境界でretuneまたはstop/reopenを行う。選択した1つのruntimeで実施する。 | 影響しないと根拠なく決めて10分回帰だけで済ませること |

短時間回帰の合否はSPECの受入条件で判定し、shellの終了値だけで決めない。receiver 7のexit 8も自動的な合格・不合格どちらにもせず、10.2.6aの既知burst条件を適用する。

### long soak の単一OS選定

long soak を実施する場合、release あたり1つの runtime/access path だけを選ぶ。選定は SPEC 10.5.2 に従い、次の順で行う。

1. 変更が作用する path-specific な環境だけに絞る。
2. 要求 topology を安全に実行できない環境を除く（例: Q3U4 の 8 receiver 用 RF・電源を用意できない環境）。
3. 残った中で canonical 環境順 **E03、E01、E02、E04、E05、E06、E07** の先頭を選ぶ。

選択しなかった影響環境は、その環境に該当する10分の targeted 確認だけを行う。release record に `単一OS規則によりsoak非該当（選定=E-ID）` と理由を記録する。glibc と musl の差は §4 の短時間回帰で覆い、両 libc を soak しない。FD数とRSSは手順どおり記録し、数値の合否基準は設けない。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、その soak は `判定保留` として pass にしない。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。

### Q3U4 2時間soak

SPEC 10.2および10.5.2の条件で、8 receiverのISDB-T/S混在負荷、反復status/APDU、定期的retune/stop/reopen、FD数・RSSの経時傾向、正常/異常cleanupを確認する。cycle境界でretuneまたはstop/reopenを行う。receiver 7でburstが出た場合は、10.2.6aのreference comparisonを fresh に行う条件かを判定する。比較を行わない場合も、baselineを継承できる理由を記録し、単にreceiver 7のexit codeを無視しない。

FD数とRSSは手順どおり記録し、数値の合否基準は設けない。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、この soak は `判定保留` として pass にせず、release前に原因を調査する。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。この disposition は soak の受入規則であり、新しい soak trigger でも、trigger のない追加 run を要求するものでもない。

### 単一receiver の30分認定

Q3U4へ影響しない追加profile固有の変更では、変更対象の各profileについて exact candidate で SPEC 10.2.7 の canonical Linux x86_64 認定を行い、30分以上連続受信する。PX-M1UR は T/S と card を持つ代表、PX-S1UR は S1UR 固有コードの変更時に加える。受信・カード・給電を伴う認定は同一OS上で順に接続して行い、両者を同時接続しない。

### PX-M1UR / PX-S1UR の同一serial確認

v0.26 の識別変更が対象のとき、両機種を同時接続できる環境では、§2 の exact candidate を使って `px4d --list` と `px4d --list-json` を個別に実行し、同じ serial の2筐体が別機種・別USB位置として現れ、双方の `serial_unique` が `false` であることを記録する。通常列挙を許さない Termux では、このコマンドの成功を要求しない。

実施可能なら、同じ状態で `px4d --device 000000000000001 --firmware "$FW" --runtime-dir "$RT"` の曖昧指定が候補と `--usb-path` を示して exit 2 となり、USB interface の claim と endpoint 公開の前に失敗することを確認する。stdout/stderr・終了コードとendpoint残存の有無を保存し、claim前拒否の根拠には模擬USB試験も対応付ける。この同時接続中は、受信・カード・LNB給電・電源制御の試験を行わない。物理USBの抜き差しは利用者の確認を得て行う。実施できない環境では未実施と理由を記録し、順次接続のcanaryや認定結果を同時接続時の識別結果へ流用しない。

### USB/cardの物理抜差し

release canaryでは、USB detach/reconnectはSPEC 10.5.1でそのpathへの影響がある場合だけ行う。それ以外のStable releaseでは行わない。新しい`tuner-hardware-verified` / `card-core-hardware-verified` claimを認定する場合は、SPEC 10.3が要求する各抜差しをそのclaimのqualificationで行う。必要な場合、対象deviceを明示してユーザーが手動で実施し、再列挙と復旧後の動作を確認する。必要な抜差しができなければ、影響を受けるclaimを未認定と記録する。

## 5. 結果の記録と公開可否

ハードウェア試験は [`platforms/validation-results.md`](platforms/validation-results.md) に日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。Stable release recordには少なくとも次を残す。

- version、source commit、candidate workflow run、9 archiveと外側checksumの確認結果、toolchain/build input、static/dynamic link inventory、relink結果、license/corresponding-source条件
- 前回Stable tag、使用したbaseline evidence、baseline以後の累積差分、各artifactのbinary byte-identity判定
- 変更impact分類と hunk-level の call-path / guard 適用条件、対象/除外したmodel-profile・runtime/access path・featureと根拠
- claimごとの `継承` / `今回再検証` / `未認定` / `対象外`、exact candidateでの試験有無
- canary/soakの選定理由（環境ID、固定順の位置、単一OS規則による非該当）
- 各 hardware test の環境ID・host・device/USB ID・runtime/access path・archive SHA-256・日時（UTC）・コマンド・counter・結果・ログ保存先
- long soak、receiver 7 fresh comparison、USB/card抜差しについて `実施` / `非該当` / `未実施` と理由
- soakのFD数・RSS記録系列と、`安定` / `頭打ち` / `判定保留` のdispositionおよび理由
- 残存blocker、既知の非blocking制限、README/support表示とrelease noteへの反映

失敗した試行は消さずに記録し、原因を切り分けた後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまでStable公開へ進まない。

- Stable共通のCI、candidate artifact audit、source/license、checksum gateが成功。
- exact final candidate canaryが成功し、必須trigger付き再認定・soakが完了。long soakを実施した場合は、releaseあたり1 OS/runtimeである。long soakのFD数・RSSが最終観測まで安定化しない持続的な増加を示し、外部要因を特定できない場合は `判定保留` とし、公開しない。
- READMEの各support claimが実証または適格なbaseline継承に対応し、未試験のmodel × runtime/access path × featureを認定表示していない。
- crash、hang、use-after-free、stale lease、再接続不能、カード経路の重大な未解決issueがない。
- Linux aarch64などの既知の hardware-unverified は、その状態のままsupport表示と短いrelease noteに反映。
- SPEC 10.5.2の再現性要件を、独立clean rebuildが未実施であることを隠してpass扱いしない。独立buildを求めるかどうかはSPEC/CIに受入条件を定義してから判断する。
- README、LICENSE、THIRD_PARTY_NOTICES、provenance、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。
- 現行 inventory に無い機種は実機未検証としてREADMEに明示すればリリース可能。Betaを機種追加の代わりに使わない。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。

## 6. 環境別手順（E01–E17）

各項は §0.1 の共通部品と次の追加手順で構成する。物理操作（USB/card の抜差し、アンテナ、電源、kernel module blacklist変更）はユーザーが事前確認のうえ実施する。HAOS（E01/E02）は production host であり、Supervisor 状態変更は承認・退避・復元を伴う。サービスを再起動しない。

### E01 HAOS x86_64 Debian/glibc SCS（canonical, container root）

- 前提: `ha core info` で HAOS version と kernel を記録する。container root 実行であることを記録し、非root claim に使わない。`lsusb -t` で kernel px4 driver が掴んでいないことを確認する。`linux-glibc-x86_64` と glibc IFD を使う。
- 手順: §0.1 の X-PREP / X-START / X-LOAD / X-MON / X-STOP / X-RESID。Q3U4 は §0.1 の preflight を先に行う。PC/SC は container の `pcscd` を使い、終了後に元の状態へ戻す。
- 選定: 影響artifactが `linux-glibc-x86_64` のとき、または HAOS container 層が変わったとき。

### E02 HAOS x86_64 Alpine/musl add-on（canonical, Supervisor）

- 前提: Supervisor add-on の options を GET 相当の現行値で退避し、ユーザー承認のうえ試験用 options を適用して add-on を起動する。終了後に options を復元して add-on を停止する。`ha`/Supervisor の状態変更はこの範囲に限り、HA Core・mirakc を再起動しない。`linux-musl-x86_64` と musl IFD を使う。
- 手順: E01 と同じ。Q3U4 は §0.1 の preflight を先に行う。物理ホットプラグを行う場合はユーザーが実施する。
- 選定: 影響artifactが `linux-musl-x86_64` のとき、または add-on 層が変わったとき。

### E03 AnduinOS x86_64（canonical Linux x86_64）

- 前提: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し、`lsusb -t` で未bindを確認する（siano側の kernel module を掴む場合）。px4 は kernel driver 不要だが、他製品の module 干渉がないことを確認する。udev rule と `video` グループを用意し、非root で実行する。
- 手順: §0.1。Q3U4 は §0.1 の preflight を先に行う。long soak の最初の候補。X-R7REF（receiver 7 参照比較）を行う場合は `tsukumijima/px4_drv` を一時 load し、同一個体・antenna・power・firmware・frequency・duration で取得後 unload する（module unload もユーザー確認対象）。
- 選定: Linux x86_64 の artifact-level targeted 検証、long soak の第一候補、canary の第一候補（影響 artifact なしのとき）。

### E04 macOS arm64（canonical darwin）

- 前提: `shasum -a 256 -c SHA256SUMS`。`otool -L` で libusb dylib と macOS system 以外の依存がないことを確認する。`sw_vers` を記録する。PC/SC は Homebrew `pcsc-lite` と `ifd/px4-userland-ifd.bundle` を使う。
- 手順: §0.1（`stat -f %z`、`lsof -p`、`ps -o rss=` を用いる）。`darwin-arm64` を使う。
- 選定: `darwin-arm64` の影響時、canary 順で E03/E01/E02 の次。

### E05 Android Termux aarch64（canonical android-aarch64, 2-FD）

- 前提: Termux と Termux:API（`termux-usb`）、`util-linux`（`setsid`）を用意する。`android-aarch64` の `px4-termux` を使う。native PC/SC adapter は N/A（card は portable IPC 経由）。
- 手順: §0.1 の Termux 2-FD。launcher の SIGINT/SIGTERM/SIGHUP と 40秒猶予、第2 open 失敗、終了後 process/FD/endpoint 残存なしを確認する。
- 選定: `android-aarch64` の影響時、launcher/FD handoff 変更時、canary 順。

### E06 Android Termux armv7a（canonical android-armv7a, 2-FD）

- 前提・手順・選定: E05 と同じ。`android-armv7a` を使う。APK は対象外であり、同 device の APK 試験は dtv-android 側で行う。

### E07 Bliss OS x86_64 Termux（canonical android-x86_64, 2-FD）

- 前提: 給電中の画面常時点灯と Termux 前面表示を検証中だけ使い、`stay_on_while_plugged_in` を実行前の値へ戻す。`android-x86_64` を使う。
- 手順: E05 と同じ。初回起動の `TIMEOUT` が起きた場合は初回起動の信頼性を未確定として記録し、合格値に埋めない。launcher に渡した device path を記録する。
- 選定: `android-x86_64` の影響時、canary 順。

### E08 Fedora x86_64（SELinux, historical-only / conditional）

- 前提: `packaging/fedora/**` または Fedora 構成手順を変更したときだけ実施する。record `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` は履歴であり current claim ではない。
- 手順: [Fedora 手順](../packaging/fedora/README.md) の手順、offline の `scripts/test-fedora.sh`、SELinux domain、一般ユーザー IPC、PC/SC、`ausearch -m AVC`。
- 選定: 当該材料の変更時のみ。canary pool・long soak の対象にしない。

### E09 Gentoo x86_64（historical-only / conditional）

- 記録は未commit差分に基づくため再現可能な current claim ではない。Gentoo 手順を新設・変更した場合に §0.1 を native build と OpenRC/udev `usb` グループ構成で実施する。それ以外は履歴のみ。

### E10 NixOS x86_64（historical-only / conditional）

- 前提: [NixOS 構成例](platforms/nixos.md)。record `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` は履歴。
- 手順: §0.1 を native build で行い、宣言的 blacklist/udev を確認する。選定: 構成手順の変更時のみ。

### E11 Chimera Linux x86_64（historical-only / conditional）

- 前提: [Chimera 構成例](platforms/chimera-linux.md)。record `3e6e32a107566689a9b4bf223dca1e4e89f67cfa` は履歴。
- 手順: §0.1 を Clang/musl native build で行い、8 receiver と direct APDU を確認する。選定: 構成手順の変更時のみ。

### E12 Alpine x86_64 / mdev（historical-only / conditional）

- 前提: [Alpine/mdev 構成例](platforms/alpine-mdev.md)。`packaging/mdev/**` は配布物に含まれるため、変更時はこの環境を実施する。offline の `scripts/test-mdev.sh` も使う。record `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` は履歴。
- 手順: `mdev -s`、非root 実行、8 receiver、direct APDU、cleanup。選定: `packaging/mdev/**` または構成手順の変更時のみ。

### E13 OpenWrt x86_64（historical-only / conditional）

- record は `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` と同内容で、OpenWrt 向けソース修正はない。OpenWrt 手順を新設・変更した場合にだけ実施する。record なしのため current claim にしない。

### E14 FreeBSD x86_64（対象外）

SPEC §1 の対象環境（Linux、Android、macOS）に含まれないため 対象外。validation-results.md の FreeBSD record は履歴であり、release gate・canary・long soak に含めない。

### E15 Fedora aarch64（unverified）

- 現状: `build-tested / hardware-unverified`。record `df6a1e634e5bec11961a1f0f15eedd1da22f7fee` は履歴であり current claim ではない。
- 手順: 新たに aarch64 claim を立てる場合だけ、`linux-glibc-aarch64` で SPEC 10.3 の tuner 項目、card、glibc aarch64 IFD/PC/SC を §0.1 に沿って実施する。kernel を記録し、claim をその kernel に限定する。昇格は direct な exact-candidate 認定による場合だけで、継承や他 architecture の結果で行わない。
- 選定: claim 変更時のみ。canary pool・long soak の対象にしない。

### E16 Debian x86/i386（source-build-only）

- i386 配布 artifact は存在しない。source archive を native build し、CTest と Q3U4 の T/S 同時受信、内蔵 card APDU を確認する（[Debian i386](platforms/debian-i686.md)）。record は Stable `v0.1.3` source archive であり履歴。選定: 該当構成手順の変更時のみ。

### E17 Windows 11 x86_64（対象外）

px4 は Windows を対象外とする。Windows 利用者向けの `tsukumijima/px4_drv` は別製品・別 interface であり、本リポジトリの release gate・canary・long soak に含めない。
