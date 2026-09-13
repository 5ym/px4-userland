# AppArmorで実行する際の注意

> **検証済み構成について:** 本文書は、一時的な自作profileを用いた実機検証の記録と、
> その際に判明した運用上の注意です。本リポジトリはAppArmor profileを同梱しておらず、
> 任意のprofileや将来のOS更新における動作を保証するものではありません。

## 検証対象

- 検証日: 2026-09-14
- `px4-userland`: Stable v0.1.3 Linux glibc x86_64配布アーカイブ
- ホスト: Latitude 5300 / AnduinOS / Linux 7.0.0-31-generic / AppArmor 5.0.2
- チューナー: PX-Q3U4（USB ID `0511:084a`、2 USB機能）
- 起動方法: 名前付きprofileをloadし、`aa-exec -p`から`px4d`を直接起動

## 実機で確認したこと

- USB device nodeを許可しないenforce profileでは、profile内の`px4d`自身による2 nodeの
  native openが有限時間内に失敗し、両nodeへのAppArmor denialが記録された。
- allow profileでは、complain 30秒、enforce 60秒、enforce 30分を完走した。
- 30分ではreceiver 0〜6のsync / TEI / continuity / queue / USB errorは0だった。
  receiver 7は同一試験個体で追跡中の既知burstの範囲内だった。
- `px4ctl status`は157/157、direct card APDUは157/157成功し、全応答末尾が`90:00`だった。
- 30分間の157測定でdaemonのFDは29で固定し、RSSは94,812〜114,820 KiBで単調増加しなかった。
- 合格runのAppArmor denialとkernel Oops / OOM / USB reset / disconnectは0で、終了後に
  profile、process、専用runtimeの残留がないことを確認した。

詳細な結果は[OS・環境別の検証結果](validation-results.md)を参照してください。

## Profile設計の要点

- AppArmorが有効なLinuxでも、profileへ入っていないprocessは`unconfined`のままです。
  `aa-exec -p`またはservice manager側の設定で、実際の`px4d`が意図したprofileへ入ったことを
  `/proc/<pid>/attr/current`などで確認してください。
- 実測したprofileでは、`px4d`、firmwareの読み取り、対象USB device nodeの読み書き、
  USB sysfsとudev databaseの読み取り、netlink、signal、Unix domain socket、専用runtime directoryへの
  読み書きを許可しました。
- USBを所有するのは`px4d`です。今回の構成では`px4-ts`と`px4ctl`はprofile外から専用runtimeのIPCへ
  接続し、USB device nodeを直接開いていません。クライアントまで同じprofileへ入れる場合は別途設計・検証が必要です。
- firmwareとruntime directoryは専用service accountから通常のファイル権限で利用できる所有権にしてください。
  所有権の不一致を`dac_override`や`dac_read_search`で迂回する構成は、この検証結果からは推奨しません。
- 今回のカード試験は`px4ctl`によるdaemon direct APDUです。IFD Handlerを`pcscd`から読み込む経路は
  AppArmor拘束下で検証していません。`pcscd`側のprofile、IFD library、runtime directoryへの許可は別途必要です。

## USB device nodeは固定パスではない

`/dev/bus/usb/BBB/DDD`のbus番号とdevice番号は、抜き差しや再列挙で変わります。
検証時に使った番号を恒久profileへコピーしないでください。

USB nodeを個別に許可する場合は、profileの生成・reload直前にsysfsの`busnum`と`devnum`からnodeを求め、
VID:PID、2機能それぞれのserial、物理USB pathを照合してください。2 nodeが同一Q3U4の期待する組であると
確認できない場合はprofileをloadせず停止するfail-closedな構成にしてください。再接続後は同じ確認と
profileの再生成・reloadが必要です。

## Deny試験の注意

USB拒否を検証するときは、`px4d`自身をprofile内でnative列挙・openさせてください。
profile外で開いたfile descriptorを`--fd`で渡す試験では、`px4d`によるUSB node openの拒否を証明できません。
また、firmwareの所有権や親directoryの探索権限に問題があるとUSBより先に失敗するため、deny原因は
AppArmor audit logの対象pathまで確認してください。
