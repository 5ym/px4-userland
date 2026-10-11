# Windows application-local WinSCard

Windows x64 ZIPの`winscard/WinSCard.dll`は、起動済み`px4d`の内蔵readerをB25 applicationから使うための限定adapterです。
daemonと同じ版を使ってください。一般WinSCard全APIの置換ではありません。cross-build/mock/native CIと実application・実cardの確認は別で、
実機受入前のapplication互換性は未認定です。既存Phase 1の受信・直接APDU実測も、新規DLLの実測を代用しません。

## 配置と接続

1. x64版のapplicationとdecoderを用意します。x86版applicationやnative ARM64版applicationではこのDLLを使えません。
2. ZIPの`winscard/WinSCard.dll`を対象applicationのEXEと同じdirectoryへ配置します。EDCBで別directoryのB25Decoderを選んでも、
   そのdecoder directoryだけに置けば依存DLLを解決できるとは限りません。元から同名DLLがある場合は置換前に退避し、用途を確認してください。
3. 対象デバイスの`px4d`を同じuserで先に起動します。DLLはdaemon、firmware、USBを自動起動しません。
4. 通常は`%LOCALAPPDATA%`配下の既存instanceを探索します。独自の`--runtime-dir`を使う場合は、application起動前に
   `PX4_WINSCARD_RUNTIME_DIR`へ同じruntime parentを設定します。`px4-userland`自体や`control.sock`を指定しません。
   短いlocal drive絶対pathを使います。UNC、network drive、junction/symlink、曖昧なpath表記は受け付けません。
5. readyなinstanceは`PX4 userland (<instance>)`として見えます。OS側の外部readerも同じ限定APIで利用できます。

OSのKnownDLLや既存loaded moduleはapplication directoryより優先される場合があります。配置だけで解決したと断定せず、
実際にロードされたDLLのpathを確認してください。System32/SysWOW64を変更したり、KnownDLL設定やservice/IFDを登録する手順はありません。
DLLはOS WinSCardを絶対system pathから読み込み、期待するfile identityを検査して外部reader操作を転送します。

## APIの範囲

- `SCardEstablishContext` / `SCardReleaseContext`
- `SCardListReadersA/W`（NULL group、呼出し側buffer、size query、二重NUL）
- `SCardConnectA/W`（内蔵readerはSHARED/EXCLUSIVE、T=1）
- `SCardDisconnect`（内蔵readerはLEAVE/RESET）
- `SCardTransmit`（内蔵readerは最大4,096 byte、応答不足時もAPDUを自動再送しない）
- `SCardIsValidContext` / `SCardFreeMemory`
- `SCardStatusW`（接続handleを検証し、接続時の実ATR、T=1、reader名を返す）
- `SCardGetStatusChangeW`（timeout=0、reader 1個、current state=UNAWAREのsnapshotだけ）
- `g_rgSCardT1Pci`（SDKと同じ8-byte DATA object）

ListReaders A/WとStatusWのname/ATRはAUTOALLOCATEを使えます。FreeMemoryは同じcontextで返されたbufferだけを解放し、
contextごとの64 allocation/計1MiB上限を設けます。ReleaseContextは残存allocationを回収します。
StatusのATRは切り詰めません。33-byte ATRに対しcallerが32 byteしか用意しない場合は必要長33とbuffer不足を返します。
GetStatusChangeWのtimeout=0はevent待ちをしない意味です。IPCの有限待ちはあり、未給電時のCARD_STATUSは一時power/UART init/offを伴います。
接続前のPRESENTでATRが空でも正常です。Statusのscalar stateとGetStatusChangeのstate bitmaskは異なります。

named importだけを契約とし、ordinal互換は保証しません。StatusA/GetAttrib、blocking/multi-reader status-change、Cancel、
Reconnect、transaction API等は公開しません。GetStatusChangeWの非zero timeout、reader数が1以外、current stateが0以外は明示的に拒否します。
未実装effectを成功扱いしません。外部readerの機能も上記exportから呼べる範囲です。
一覧はlocal先、OS後で、同名ならOS readerを優先します。local探索は64 entry、1.5秒、endpointごと200msの協調的deadlineを使います。
同期filesystem/security処理やOS providerの待ち時間を含め、呼出し全体が必ず1.5秒以内に戻るという保証ではありません。
上限超過は部分的なPX4一覧を返さず、次の呼出しで再探索します。local設定/探索に失敗しても、利用可能なOS readerは独立に使えます。
OS readerもない場合はlocal errorを返します。失敗したlocal reader名をOS側へ転送しません。

新DLLはHELLOでCARD_GENERATION_BINDINGを必須交渉します。このbitはCARD_HANDLE_STATUSも含む契約です。古いdaemonはreaderに含まれません。
daemonが抜去を観測したhandleでは、再挿入後も暗黙に別cardへAPDUを送りません。applicationは切断して新規接続する必要があります。
Statusも所有handleをdaemonが原子的に検証します。reader全体のgenerationが同じという理由で古いhandleを有効扱いしません。
IPC利用者の明示CARD_RECONNECT/CARD_RESETは現在のcardへの再接続として扱います。既存IFD/CLIはこのcapabilityを要求しない限り従来動作です。
物理変化がdaemonから観測できない場合まで識別できるという保証ではありません。USB切断後にdaemonを再起動した場合も古い接続を自動復活しません。

## 対象clientの根拠

次はsourceで確認したimport範囲です。アプリ全体の実測合格を意味しません。第三者実装を本DLLへコピー・同梱していません。

- [recisdb-rs d30c6b5](https://github.com/kazuki0824/recisdb-rs/tree/d30c6b5e4724508840504747df89b4d4defaa944)の
  [Windows link設定](https://github.com/kazuki0824/recisdb-rs/blob/d30c6b5e4724508840504747df89b4d4defaa944/b25-sys/build.rs)と、
  gitlink先[libaribb25 1221389 / b_cas_card.c](https://github.com/tsukumijima/libaribb25/blob/12213899010738acaadd7fd945c9e25d35561af7/aribb25/b_cas_card.c)：
  上記6 familyとT1 PCIを使用。失敗した初期化ではRESET、通常終了ではLEAVEを使用。
- [xtne6f/EDCB ebf50c7](https://github.com/xtne6f/EDCB/blob/ebf50c730ccf8c1732e0bd8a4e2a3417a94d9b2b/BonCtrl/ScrambleDecoderUtil.cpp)は
  選択されたB25Decoderをロードするhostです。同じloaderを持つtkntrec版/tsukumijima版を含め、PC/SC要件は選択decoderで決まります。
  [Multi2Dec ccc80cd / BcasCardReader.cpp](https://github.com/tsukumijima/Multi2Dec/blob/ccc80cdb59b370057cecf3e1a965027fc2fb549b/B25Decoder/BonProject/BcasCardReader.cpp)
  の経路は上記6 familyとT1 PCIを使用します。
- TVTest経路は[DBCTRADO/TVTest dce8b3e](https://github.com/DBCTRADO/TVTest/tree/dce8b3e5e19cb6b1b12c60ff839b943a0685d715)、
  [logue/CasProcessor c26423d](https://github.com/logue/CasProcessor/tree/c26423d845bfc4efde8a5492a00ee25827877670)、
  [logue/TvCas 692fbdd / CardReader.cpp](https://github.com/logue/TvCas/blob/692fbddfb562f50f3241cfc58ae4b38c8dcfafe4/TVCAS_25/CardReader.cpp)
  のRelease x64、Unicode、B25.tvcas、SmartCard reader選択を対象にします。追加4 APIとAUTOALLOCATEはこの通常経路の要求です。
  任意のTVTest版/CAS pluginやSPHD/B1を保証しません。このprojectで無効なdynamic `.scard`経路を配置の代用にしません。
- [Microsoft DLL search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)も配置の条件です。

## 検証と実機への引継ぎ

cloud側のgateは、production DLLのPE/export/DATA/import監査、native Windowsのtyped mock IPC、provider併合、A/W buffer、
stale/foreign handle、ReleaseContext競合、SDK importによる実DLL path/System32 provider確認、exact ZIP展開後の同じ確認です。
Linux上のcross-buildはnative Windows実行の代わりではありません。実APDU、カード識別情報、TS、vendor binaryをCIへ持ち込みません。

実機試験では、candidate/run/hash、Windows版、application名・版・bitness、decoder名・版・bitness、実際のWinSCard module pathを記録します。
既存daemonでのreader表示、内蔵card初期化/APDU、decoderの通常終了・初期化失敗時RESET、外部reader併用、application再起動、
card抜去/再挿入とUSB切断後の明示復旧を別項目にします。抜差し等の物理操作は利用者の確認後だけ行います。
EDCB/recisdb-rs/上記TVTest chainでTS処理した結果と直接IPC APDUの結果は分けて記録します。
対象profile・試験時間・soakは既存release手順と利用者の判断に従います。

TvCasは起動20秒/終了5秒を超えるとworkerを強制TerminateThreadし、ECMのretryやcard再openも行います。
DLLのCARD requestは各4秒でtimeoutしますが、host側のretry累積は5秒を超え得ます。強制終了中のlock/resource回収は保証できません。
exact pluginでstalled daemon、終了、再起動を検証するまで、正常時のmock成功をshutdown安全性へ拡張しません。
またSCardSvr設定の検査は通常openの条件でなくerror時診断です。OS service不在でlocal readerを使えても診断表示が紛らわしい場合があり、
検証のためにservice設定を自動変更しません。
