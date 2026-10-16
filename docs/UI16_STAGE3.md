# UI16 段階3：機種一覧とゲーム一覧の移動

基準：ak188571/firmware-bl616 の ui16-uart、commit `9d89b52d5f4886511992ac52ee9033d24dd17fc0`。
段階2の上下選択を実機確認済みの状態へ適用する。

## 操作

| 画面 | 操作 | 動作 |
|---|---|---|
| 機種一覧 | 上下 | 機種・項目を選択。長押し400/100 msは段階2と同じ |
| 機種一覧 | B | 選択機種のROMフォルダを読み、ゲーム一覧へ移動 |
| ゲーム一覧 | 上下 | 表示されているファイルを選択 |
| ゲーム一覧 | A | 機種一覧へ戻り、入る前の機種の選択位置を復元 |
| ゲーム一覧 | B | この段階では何もしない。ROMは起動しない |
| 機種一覧 | A | この段階では何もしない |
| CORES / OPTIONS | B | この段階では何もしない |

Bは入力bit `0x0001`、Aは `0x0100`。従来メニューと同じ割当。
決定と戻るは押した瞬間だけ受け付け、画面が変わっても押下状態を保持する。
A+B同時押しでは画面を移動しない。
ゲーム一覧へ入り直すとフォルダを読み直し、先頭ファイルを選択する。

## ファイル取得と表示

既存のFatFs API `f_opendir` / `f_readdir` / `f_closedir` を使い、`FILINFO.fname` を文字列として保持する。
既存のFileChooserのページ表示や戻る行は使わず、UI16の独立した一覧画面で描画する。
長いファイル名を保持するが、表示幅を超える部分は切り詰める。
日本語ファイル名の文字コード設定とフォント拡充は段階4で行う。今回は既存のFatFs設定を変更しない。

対象の場所と拡張子（大文字・小文字を区別しない）：

| 機種 | フォルダ | 拡張子 |
|---|---|---|
| NES | `nes/` | `.nes` |
| SNES | `snes/` | `.smc`、`.sfc` |
| Game Boy Advance | `gba/` | `.gba` |
| MegaDrive / Genesis | `genesis/` | `.bin`、`.md` |
| Sega Master System | `sms/` | `.sms` |
| IBM PC/XT | `pc/` | `.img` |

実装ではフォルダを固定文字列にせず、選択機種の既存 `rom_dir` を参照する。
起動時に選ばれた既存の `drv` を使う。microSDなら `sd:nes` 等、USBへfallbackした場合は `usb:nes` 等。

- 対象フォルダ直下の通常ファイルだけを取得する。子フォルダ、隠し属性、system属性のファイルは除外する。
- 保存順はFatFsの列挙順。並べ替えは追加していない。
- 最初の10件を保持・表示する。11件目の対象ROMがあれば下部に `MORE FILES` と表示する。
- 11件目以降への移動や縦スクロールは段階5で追加する。
- 機種名を見出し、各ファイル名を本文、下部に `A BACK` を表示する。
- 段階1・2と同じフォントを使う。英小文字は大文字表示になり、未収録記号（`.`等）は空白になる。保持する元のファイル名は変更しない。
- 空一覧は `NO ROM FILES`、フォルダを開けない場合は `DIRECTORY ERROR`、列挙・終了時のエラーは `READ ERROR`。
- エラーでは途中までの一覧を破棄する。どの状態でもAで機種一覧へ戻れる。

## 適用

ZIPの `apply_ui16_stage3.py` と `ui16_stage3.patch` を firmware-bl616 直下へコピーする。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
python apply_ui16_stage3.py
```

登録済みファイルに未commitの変更がある場合は、先にcommitする。
scriptは適用前の検査が成功した場合だけ変更し、commitやpushは行わない。

## build・書込み

```bash
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"

make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

新しい.cppを追加するため、上記make経由でCMake設定を再実行する。
`build/build_out/tangcore_bl616.bin` をDevCubeで `0x40000` に書き込む。
FPGAの再合成・再書込みと、microSDのmonitor.binの差し替えは不要。
完全に電源を切ってPCとのUSB接続を外し、電源アダプタからBOOTを押さずに起動する。

## 実機確認

1. microSDのnesやsnes直下に、正しい拡張子・英数字中心の名前のROMを少数用意する（各10件以内）。
2. 機種一覧でSNES等へ移動し、Bでゲーム一覧へ入る。
3. 見出しが選んだ機種名になり、そのフォルダの対応拡張子のファイルだけが出ることを確認する。
4. 上下で選択色を動かし、Aで戻る。元の機種が選択されたままであることを確認する。
5. Bを押し続けても一覧の再読込みやROM起動が繰り返されないことを確認する。
6. ROMがない機種やフォルダがない機種で、状態表示が出てAで戻れることを確認する。
7. 必要なら11件以上のROMでMORE FILES表示を確認する。これはスクロール未実装を示す。

## 検証

```bash
bash tools/ui16/test_system_menu.sh
bash tools/ui16/test_navigation.sh
bash tools/ui16/test_browser.sh
```

既存の段階1・2テストは成功。
段階3はFatFsのホスト用代替を使い、機種別拡張子、大小文字、長い名前、除外属性、件数制限、open/read/closeエラー、画面遷移、押下edge、選択位置復元、空・エラー一覧からの復帰、表示と994 byte送信を検証した。
AddressSanitizer / UndefinedBehaviorSanitizerで異常なし（環境制約でLeakSanitizerは無効）。
一覧・件数超過・空・各エラー画面を、既存のVerilator生成済みモデルでUART受信・全画素比較した。今回はRTL変更・モデル再生成なし。
BL616向けcross build、実microSDのアクセス、今回変更の実機動作は未確認。お手元で確認する。

## 実機確認後のcommit

```bash
git add main.cpp ui/ui16_system_menu.h ui/ui16_system_menu.cpp \
  ui/ui16_game_list.h ui/ui16_game_list.cpp ui/ui16_browser.h ui/ui16_browser.cpp \
  tests/ui16/ff.h tests/ui16/browser_test.cpp tools/ui16/test_browser.sh docs/UI16_STAGE3.md
git diff --cached --stat
git commit -m "Add UI16 game list navigation and return to systems"
git push fork ui16-uart
```

build出力・適用用patch・適用scriptは追加しない。親のtangcoreにもfirmware-bl616の参照更新をcommitする。
