# UI16 段階1：機種一覧の固定表示

基準：ak188571/firmware-bl616 の ui16-uart、commit `6c1419a36043a48e61b7cfd7bc09ffdfd0a29d4e`。
動作確認済みの monitorcore `e472e929d63a3bda85a2e67f83c51d1aa79309fd` のフォントとUART形式を使用する。

## 動作

- `main_menu_config` の順番で表示し、正のIDは `find_core_by_id()` で取得する。存在しないIDは省略する。
- ID 1は「ファミリーコンピュータ」、ID 2は「スーパーファミコン」。他の機種は既存の表示名を英字大文字に変換する。
- 既存の CORES / OPTIONS も末尾に表示する。今回の設定では機種6件＋操作項目2件。
- 先頭の機種だけ選択色。画面は14行×32半角セル、見出しは1行目（0起点）、項目は3行目から。
- `monitor.bin` の読込み成功後、1秒待って画面全体994 byteを送信し、1秒ごとに再送する。遅れて出る従来のUSB状態表示からも復帰する。
- この段階ではコントローラ入力、ゲーム一覧への移動、ROM起動は接続しない。
- 最大10項目を表示する。縦スクロールは後段階で実装する。
- 半角英字は8 pixel、日本語は16 pixel。既存のglyph番号をそのまま使用する。
- フォント未収録文字は幅を保って空白にする（既存フォントでは `/` も空白になる）。長い項目は行末で切り詰め、全角文字を途中で分割しない。

## 適用

ZIP内の `ui16_stage1.patch` と `apply_ui16_stage1.py` を firmware-bl616 の直下へコピーする。
UCRT64で次を実行する。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
python apply_ui16_stage1.py
```

適用scriptはGit登録済みファイルに未保存の変更があれば中止する。先にcommitしてから実行する。
`git apply --check` が成功した場合だけpatchを適用する。二重適用はできない。

## build

前回の環境設定を含むコマンド：

```bash
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"

make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

新しい `.cpp` をCMakeの既存のglobへ追加するため、上記make経由でCMake設定を再実行する。
`UI16_SYSTEM_MENU` と `UI16_UART_SELFTEST` を同時にONにすると設定時にエラーにする。

## 実機確認

1. build成功後、`build/build_out/tangcore_bl616.bin` をDevCubeで `0x40000` へ書き込む。
2. microSDの `cores/console138k/monitor.bin` は、3行表示で動作確認済みのUART版のまま使用する。
3. PCへのUSB接続を外し、電源を完全に切って、電源アダプタからBOOTを押さずに起動する。
4. 上部にSYSTEMS、続いて機種6件とCORES / OPTIONSを表示する。先頭機種だけ選択色になる。
5. 以前の試験用3行と画面下部のTangCoreロゴが新しい画面に置き換わることを確認する。

FPGAの再合成・再書込み、microSDのmonitor.binの差し替えは不要。
PC接続時はBL616がdebuggerとして動作するので、実機表示は電源アダプタで確認する。

## 試験表示へ戻す

同じbuildコマンドの設定を `UI16_SYSTEM_MENU=OFF UI16_UART_SELFTEST=ON` に変え、生成binを書き直す。
通常の従来メニューへ戻す場合は両方OFFにする。

## ホスト側検証

```bash
bash tools/ui16/test_system_menu.sh
```

日本語の左右セルとglyph番号、設定順・選択色、空一覧、項目数上限、長い名前、異常UTF-8、画面全体のクリア、UART送信の長さと内容、行単位のcritical sectionを検証する。
ホスト検証はBL616向けbuildや実機動作の代替ではない。

今回の検証結果：ホストテストとAddressSanitizer / UndefinedBehaviorSanitizerで異常なし。
既存のVerilator生成済みモデルに今回の一覧セルを与え、2 MbpsのUART波形受信、全画素比較、独立clock、timeout後の復帰が成功。
生成済みモデルのRTLとtestソースは上記monitorcore commitと一致することを確認した。今回RTLの再生成は行っていない。
BL616向けのcross buildと実機試験は、この配布物では未実施。

## commit対象

実機確認成功後、変更したファイルだけを追加する。

```bash
git add CMakeLists.txt Makefile main.cpp \
  ui/ui16_system_menu.h ui/ui16_system_menu.cpp ui/ui16_font_map.h \
  tests/ui16/system_menu_test.cpp tools/ui16/test_system_menu.sh docs/UI16_STAGE1.md
git diff --cached --stat
git commit -m "Add stage-one UI16 system menu display"
git push fork ui16-uart
```

`build/`、`build-ui16-host/`、適用用patchとscriptは追加しない。
