# 第6段階 改訂版：ゲーム起動・復帰と可変OSD

第5段階のcommit `7ff2d79ec283917544169d7d0dc5224a0ca6dc1b`、または配布済み第6段階を基準とする。
**第6段階を適用済みで、まだcommit/pushしていない状態から更新できる。先にcommitする必要はない。**

## 今回の仕様

- A/BでROMを起動し、既存loaderへ実ファイル名を渡す。表示名・強制改行・字下げはパスに含めない。
- ROM、ゲーム用bitstream、復帰用monitor.binの存在を先に確認する。
- FPGA書込み→機種ID確認→可変OSD対応確認→ROM読込みの順で進む。待機には打切り時間を設ける。
- 起動失敗時はmonitorへ戻し、元のフォルダ・選択項目・表示先頭を復元する。A/Bで再試行できる。
- Main Menuでゲームを終了すると、同じ一覧位置へ戻る。ゲームの状態は保存しない。
- 一覧の2行表示、明示改行、2行目の半角スペース、縦scrollは第5段階のまま。

### ゲーム中のOSD

既定の呼出しはSELECT＋右。設定済みのSELECT＋START等にも追従する。
どれか1台のコントローラで組合せを押して離す。別々のコントローラに分割した組合せでは呼び出さない。

| 操作 | 動作 |
|---|---|
| 上下 | 項目を選択。長い一覧では表示範囲をscroll |
| A/B | 選択項目を決定 |
| X/Y または呼出し操作 | OSDを閉じ、ゲームを続ける |
| POS行で左右 | 位置をその場で切替え |
| Main Menuを決定 | ゲームを終了しUI16一覧へ復帰 |

初期位置は**右上**。中央・左上・右上・左下・右下の5箇所から選べる。
位置を変えても、その場の選択項目と設定値は保持する。位置は再びOSDを開いたときにも保持し、BL616のreset／電源断で右上に戻る。SDには保存しない。

枠は内容の行数・文字数から計算する。1つのOSDを開いている間は値の桁数が減っても幅を縮めず、表示の揺れを抑える。
32×28文字のcanvas内で最大30×26文字、本文は最大21行。超えた行は選択位置に合わせてscrollする。横に長すぎる行は表示範囲で切る。
Main Menuだけの場合はPOSと操作案内を含め19×6文字。枠の外にはゲーム映像を表示する。

OSD表示中の操作はゲームへ渡さない。呼出し操作、および閉じる操作をした後も、表示が消えて全コントローラのボタンを離すまで入力を遮断する。ゲーム自体は動作を続ける。
OSDの操作割当てはゲーム中のA/B/X/Y配置を変更しない。

ゲーム用OSDは従来の英語8×8 fontを使う。CRT風表示のパラメータそのものは今回は追加しない。
将来の設定項目はMenu::on_adjust(index, delta)で左右の値変更、osd_width()で必要な最小幅を指定できる。
項目数に応じたサイズ計算・scroll・選択保持は共通処理を使う。
PCの既存floppy選択dialogは従来の全画面表示を使い、終了後に可変OSDへ戻る。

## 適用

ZIPを親tangcore直下へ展開し、apply_ui16_stage6_osd.pyとui16_stage6_osd_packageをfirmware-bl616等と並べる。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore
python apply_ui16_stage6_osd.py --check
python apply_ui16_stage6_osd.py
```

既定ではfirmware-bl616と**NES/SNES**を更新する。使うcoreを追加する場合は、checkと適用の両方で同じ指定をする。

```bash
python apply_ui16_stage6_osd.py --check --cores nestang snestang gbatang mdtang pctang
python apply_ui16_stage6_osd.py --cores nestang snestang gbatang mdtang pctang
```

全対象を検査してから書き込む。第5段階／配布済み第6段階と一致する未commitのfirmware変更を受け入れる。
想定外の編集がある対象ファイルは上書きせず、全体を止める。coreには文脈付きpatchを適用するため、変更箇所以外のローカル編集を保持する。
USB controller関連ソースを置き換えない。既存のconsole138k等のGowin projectには新規tc_osd_layer.svの登録だけを追加する。
CRLF/LFの違いを許容し、更新対象の出力はLFとする。
適用前の元のbytesはui16_stage6_osd_backupsへ日時別で保存し、新規ファイル一覧も記録する。
index、commit、push、実機・SDへの書込みは行わない。

SMSTangはこのZIPの適用対象外。親repositoryが参照する版のソースを取得できず、実際のUART接続を検証できなかったため。
NES/SNES以外は対応ソースを同梱するが、まずNES、次にSNESを実機確認し、その他は使用するcoreから個別に進める。

## PCでの確認

```bash
cd firmware-bl616
bash tools/ui16/test_osd.sh
bash tools/ui16/test_launch.sh
bash tools/ui16/test_wrap.sh
bash tools/ui16/test_scroll.sh
bash tools/ui16/test_folders.sh
bash tools/ui16/test_titles.sh
bash tools/ui16/test_browser.sh
bash tools/ui16/test_navigation.sh
bash tools/ui16/test_system_menu.sh
bash tools/ui16/test_sender.sh
```

host testは、動的サイズ・5位置・即時移動・選択保持・将来の値変更interface・起動復帰・UART送信・旧core検出・task別の描画captureを確認する。
RTLの任意testはZIP内verification/README.mdを参照。
こちらではhost testとRTL simulationを実行した。Gowinでの全体合成、BL616向けcross build、実機表示・ROM動作は未確認。

## build・書込み

**今回からBL616と、利用するゲーム用FPGAの両方を再buildする。monitorは第5段階版を継続使用する。**
旧ゲーム用bitstreamは可変OSDと入力遮断に未対応のため、起動を中止して一覧にREBUILD GAME COREを表示する。
その場合は対象coreを更新・合成して、SDの従来と同じゲーム用.binを新しいものへ差し替える。markerを手作業で足して検査を回避しない。

Gowinで、これまで実機確認に使っていた同じboardのprojectを開き、src/iosys/tc_osd_layer.svが登録済みであることを確認して合成する。
別board用のprojectへ切り替えない。game bitstreamの名称・配置は既存loaderの設定に従う。

BL616はこれまでのconsole138k設定でbuildする。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"
make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

CMakeは新しいui16_osd.cpp、ui16_osd_menu.cpp、ui16_launch.cppを検出する。
DevCubeでbuild/build_out/tangcore_bl616.binを**0x40000**へ書き込む。

## 実機確認とcommit

第6段階を以下の小段階に分け、それぞれ実機確認してからcommitする。

| 小段階 | 作業・実機確認 |
|---|---|
| 6a | NES起動、旧coreの検出、正しいROM読込み、元の一覧位置への復帰 |
| 6b | OSDの内容に応じた枠、枠の外のゲーム映像、右上の初期位置 |
| 6c | POS行から5位置へその場で移動、選択保持、閉じて再表示した位置 |
| 6d | native/USBともOSD中にゲームが反応しないこと、閉じるボタン長押し、解放後の操作再開 |
| 6e | SNESで同じ確認。他coreは使用順に更新・確認。将来のCRT項目追加は別作業 |

配布は1つの整合した変更セットとして適用する。6a〜6dは同じ変更を順に実機確認する観点であり、実装を途中で分割してcommitする手順ではない。
まずNESで6a〜6dが通った単位をcommitし、次にSNES、その他coreを個別にcommitする。

既に適用済みの旧第6段階の変更と合わせて保存する。CHANGED_FILES.txtを参考に対象を指定してgit addし、diff --cached --checkを通す。
coreのproject登録も忘れずに保存する。USB関連の既存変更は内容を確認し、従来の保存方針に従う。
各submoduleのcommit/push後に、親tangcoreのsubmodule参照を更新する。
このZIPの適用script、バックアップ、build生成物はcommit対象に含めない。
