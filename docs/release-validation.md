# Stable リリース前検証手順

この文書は Stable リリースごとの実行順と、変更内容に応じた追加検証の選び方を定める。合否条件と認定条件の正本は [`SPEC.md`](../SPEC.md) であり、両者に差があれば SPEC を優先し、手順書を更新する。全 OS・全機種を毎回回帰する手順ではない。

## 1. 候補と前回証拠を固定する

1. 候補の `VERSION`、GitHub 上の source commit、candidate workflow run を特定する。dirty な作業ツリーや未 push のローカル変更を候補として試験しない。
2. 前回 Stable 以降の累積差分と、今回使う baseline evidence を確認する。差分をファイル名だけで判断せず、SPEC 10.5.1 の変更分類と実際の呼出経路・依存先から model/profile・runtime/access path・feature の影響範囲を決める。
3. 対象 claim ごとに `継承`、`今回再検証`、`未認定` のいずれかを記録し、根拠を書く。証拠の軸は SPEC 10.2.8 に従う。別 model、別 OS/runtime、別 access path、別 feature へ結果を外挿しない。
4. 前回の適格なQ3U4 2時間soakの日付と、それ以降のStable release数を記録し、6か月または6回目のStableのどちらかに到達していないか確認する。beta/pre-releaseはStable数に含めない。どちらかに到達していれば、変更差分にかかわらず周期gateを追加する。

Termuxのarchitecture、Termux launcherのFD path、ad-hoc APK、native PC/SC adapter、glibc/muslは別々のruntime/access pathとして扱う。Windowsは対象外、Android ad-hoc APKは内部試験経路であり配布artifactではない。変更も新規claimもないpathを毎回試験しない。

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

1. 前節で固定した final candidate artifactそのものを使う。対象 archive名と SHA-256、source commit、model/USB ID、host OS/version、runtime/libc、access pathを記録する。
2. 少なくとも1つの model/runtime/access pathで10分以上の canaryを行う。候補変更の影響を受ける範囲から、最も複雑な topology を選ぶ。共有 tuner/card/power/concurrency の変更を、単一T受信だけで代表させない。
3. 試験コマンドは[README](../README.md)のCLI仕様と対象profileの例に合わせる。選んだ profile に該当する `list/grouping`、ISDB-T/S または plain-TS、status、同一lease retune、stop/reopen、搭載時の card/APDU、正常終了を確認する。10分の受信中は SPEC 10.2/10.2.7 の該当 TS条件を満たすことを確認し、packet・byte・counter・終了statusを保存する。profileにない機能や物理構成は試さず、非該当理由を記録する。
4. 終了後に daemon/client、FD、IPC socket/control endpoint等の残留がないことを確認する。旧candidateや異なるartifactでの結果を今回の canary に数えない。
5. PX-Q3U4 receiver 7 の TEI/continuity burst は自動合格にしない。参照試験がSPEC 10.2.6aの同一個体・antenna・power・firmware・frequency・duration・同等capture条件を満たし、受入条件を満たした場合だけ既知制限として扱う。条件を再現できなければ判定保留とする。

この canary は release gate であり、それだけで全機種・全 runtime/access path の hardware claim を更新しない。

## 4. 変更影響に応じた targeted qualification

SPEC 10.5.1の分類を基準に、以下のいずれかだけを実施する。複数条件に該当する場合は同じ run が全条件を満たせば重複実施せず、結果を各 trigger に対応付けて記録する。

| 判定 | 必要な追加検証 | 要求しない検証 |
|---|---|---|
| docs / license text / package metadata のみ | 各 Stable 共通のCI・archive/license/source gateと10分canary | 新しい hardware requalification・long soak |
| 新規または未認定の追加 profile。Q3U4非影響を立証 | exact candidateで対象 profile ごとに canonical Linux x86_64のSPEC 10.2.7認定を完了し、30分以上連続受信する。影響する別runtime/access pathは個別に targeted 確認する。 | Q3U4の2時間soak。30分profile認定をQ3U4 soakの代用・同等物と呼ばない |
| 共通実装を変更したがQ3U4非影響を立証 | 差分、Q3U4のcall path、条件分岐等による非適用根拠を記録する。加えて exact candidate でQ3U4の8 receiver ISDB-T/S混在受信を、SCS native/glibc と HAOS Alpine/musl の各pathで10分以上行う。status・card APDUを併走し、stop/reopen、TS/USB counter、終了後残留を確認する。 | 上記の証拠が揃い pass なら、変更起因のQ3U4 2時間soak |
| stream/queue/demux/concurrency/lifetime/hotplug/card/power/LNB/USB transport/IPC lease等がQ3U4の長時間挙動に影響し得る、または非影響を立証する短時間回帰が未実施/失敗/曖昧 | exact candidateでSPEC 10.5.2に従うQ3U4 2時間以上の8 receiver T/S混在負荷soak。影響するruntimeで実施し、共通変更がglibcとmusl双方へ影響する場合はSCS native/glibcとHAOS Alpine/muslの双方を試験する。短時間回帰の失敗に機能不具合が含まれる場合、soakはその不具合の修正・切分けの代用にならない。 | 影響しないと根拠なく決めて10分回帰だけで済ませること |
| 周期再認定期限 | 前回の適格なQ3U4 soakから6か月、または6回目のStable releaseのうち先に到達した時点で、変更影響と独立してSPEC 10.5.2のQ3U4 2時間soakとreceiver 7 fresh reference comparisonを行う。 | profile固有変更を理由に周期gateを免除すること |

短時間回帰の合否はSPECの受入条件で判定し、shellの終了値だけで決めない。receiver 7のexit 8も自動的な合格・不合格どちらにもせず、10.2.6aの既知burst条件を適用する。

### 検証結果から更新できるsupport claim

試験を通ったこととsupport claimの認定は別である。READMEのclaimを変更するときだけ、SPEC 10.3の該当要件を満たす。

- `tuner-hardware-verified`: 対象model/runtime/pathで列挙・grouping、firmware、対応する各systemのtune/capture、stop/reopen、USB disconnect/reconnectを確認する。
- `card-core-hardware-verified`: 対象pathでATR、reset、反復APDU、card抜去/再挿入、USB disconnect/reconnectを確認する。
- `native-card-adapter-verified`: Linux/macOSは実PC/SC consumerからresetと反復APDUを確認する。Androidは`not applicable`。
- `runtime-supported`: 対象profileの全receiver同時stream中に、Linux/macOSはnative card adapter、Androidはportable IPC clientから反復APDUを行い、該当するtuner/card基準を満たす。

既存claimがbaselineから有効に継承され、今回更新しないなら再認定しない。canary、profileの30分試験、別runtimeの同一機種試験を、互いのclaimや未試験featureの認定へ流用しない。

### 追加profileの30分認定

対象機種ごとに個別に行い、別profileや別runtimeの認定で代用しない。SPEC 10.2.7に従い、device識別/grouping、profileのreceiver数・system、firmware、tune/retune、capture、stop/reopen、status、receiver同時stream/demux、該当するcard/ATR/APDU、power/LNB、cleanupを確認する。profileに存在しないsystem・card・電源機能は `N/A` または未搭載と記録し、検証済み扱いしない。

### Q3U4 2時間soak

SPEC 10.2および10.5.2の条件で、8 receiverのISDB-T/S混在負荷、反復status/APDU、定期的retune/stop/reopen、FD数・RSSの経時傾向、正常/異常cleanupを確認する。glibc/muslの両方に影響する変更は両pathで2時間ずつ行う。片方にだけ影響すると根拠付きで限定できる場合は、そのpathだけでよい。周期gateの場合は代表Q3U4 topologyの1 pathを選び、path選定根拠と前回周期gateからの経過を記録する。変更起因gateと周期gateが同時期に該当し、同じ試験で双方の条件を満たす場合は重複soakしない。

receiver 7でburstが出た場合は、10.2.6aのreference comparisonを fresh に行う条件かを判定する。比較を行わない場合も、baselineを継承できる理由を記録し、単にreceiver 7のexit codeを無視しない。

### USB/cardの物理抜差し

release canaryでは、USB detach/reconnectはSPEC 10.5.1でそのpathへの影響がある場合、または周期再認定時だけ行う。それ以外のStable releaseでは行わない。ただし、新しい`tuner-hardware-verified` / `card-core-hardware-verified` claimを認定する場合は、SPEC 10.3が要求する各抜差しをそのclaimのqualificationで行う。必要な場合、対象deviceを明示してユーザーが手動で実施し、再列挙と復旧後の動作を確認する。必要な抜差しができなければ、影響を受けるclaimを未認定と記録する。

## 5. 結果の記録と公開可否

ハードウェア試験は [`platforms/validation-results.md`](platforms/validation-results.md) に日付付きで記録する。Stable release recordには少なくとも次を残す。

- version、source commit、candidate workflow run、9 archiveと外側checksumの確認結果、toolchain/build input、static/dynamic link inventory、relink結果、license/corresponding-source条件
- 前回Stable tag、使用したbaseline evidence、baseline以後の累積差分
- 変更impact分類、対象/除外したmodel-profile・runtime/access path・featureと根拠
- claimごとの `継承` / `fresh test` / `未認定`、exact candidateでの試験有無
- 各 hardware test のhost・device/USB ID・runtime/access path・archive SHA-256・日時・条件・結果・ログ保存先
- long soak、receiver 7 fresh comparison、USB/card抜差しについて `実施` / `非該当` / `未実施` と理由
- 残存blocker、既知の非blocking制限、README/support表示とrelease noteへの反映

失敗した試行は消さずに記録し、原因を切り分けた後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまでStable公開へ進まない。

- Stable共通のCI、candidate artifact audit、source/license、checksum gateが成功。
- exact final candidate canaryが成功し、必須trigger付き再認定・soakが完了。
- READMEの各support claimが実証または適格なbaseline継承に対応し、未試験のmodel × runtime/access path × featureを認定表示していない。
- crash、hang、use-after-free、stale lease、再接続不能、カード経路の重大な未解決issueがない。
- Linux aarch64などの既知の hardware-unverified は、その状態のままsupport表示と短いrelease noteに反映。
- SPEC 10.5.2の再現性要件を、独立clean rebuildが未実施であることを隠してpass扱いしない。独立buildを求めるかどうかはSPEC/CIに受入条件を定義してから判断する。
- README、LICENSE、THIRD_PARTY_NOTICES、provenance、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。
- 未所有の機種は実機未検証としてREADMEに明示すればリリース可能。Betaを機種追加の代わりに使わない。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。
