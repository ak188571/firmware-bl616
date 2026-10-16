# UI16 段階2：機種一覧の上下選択

基準：ak188571/firmware-bl616 の ui16-uart、commit `e076ac8d98587f147cf291f68ccea95a29cb925e`。
段階1の機種名表示を実機で確認済みの状態へ適用する。

## 動作

- 既存の `get_joypad_states()` から joy1 / joy2 / hid1 / hid2 を取得し、論理ORで統合する。
- 上は `0x10`、下は `0x20`。従来メニューと同じ入力割当を使う。
- 短押しで1項目移動。長押しは400 ms後に最初の繰り返し、その後100 ms間隔。入力を約10 msごとに確認する。
- 先頭で上・末尾で下は停止する。上下同時押しは移動しない。
- 押す方向を反転したときは直ちに1項目移動し、長押しの待ち時間を数え直す。
- ボタンを離すと繰り返しを停止する。処理が一時停止しても、蓄積した移動を一度に実行しない。
- 起動時は先頭の機種を選択する。CORES / OPTIONSも選択対象とする。
- 決定・戻る、左右、Start等はまだ画面遷移やゲーム起動に接続しない。
- 最初の表示と選択位置変更時に、全14行994 byteを送る。段階1の1秒ごとの再送を廃止する。
- 表示内容・収録フォント・最大10項目という制限は段階1と同じ。縦スクロールは後段階。

## 従来文字表示による上書きの防止

`monitor.bin` 読込み成功後、UI16画面へ切り替える直前に `fpga_use_ui16_display(true)` を呼ぶ。
この間、`fpga_tx_header()` は従来文字表示command `0x05` を、同じpayloadを持つdebug用command `0x0d` に変換する。
これにより、USBコントローラの接続・切断等の通知が従来画面へ戻すことを防ぐ。debugメッセージの内容はUART上に残る。
他のcommand・payload・lengthは変更しない。
起動時は従来表示が有効。UI16_SYSTEM_MENUがOFFなら従来動作のまま。

後の段階で従来OSDを持つゲームcoreへ移行するときは、先に `fpga_use_ui16_display(false)` を呼んで表示方式を戻す必要がある。

## 適用

ZIPの `apply_ui16_stage2.py` と `ui16_stage2.patch` を firmware-bl616 直下へコピーし、UCRT64で実行する。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
python apply_ui16_stage2.py
```

scriptは登録済みファイルに未commitの変更がある場合は中止する。先にcommitしてから実行する。
`git apply --check` が成功した場合だけ適用し、commitやpushは実行しない。
既に段階2を適用済みの場合も中止する。

## build

段階1と同じ設定でbuildする。FPGA側の再合成・再書込み、microSDのmonitor.binの差し替えは不要。

```bash
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"

make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

新しい.cppの追加があるので、上記make経由でCMake設定を再実行する。
生成した `build/build_out/tangcore_bl616.bin` をDevCubeで `0x40000` に書き込む。
書込み後は完全に電源を切り、PCにつながるUSBケーブルを外し、電源アダプタからBOOTを押さずに起動する。

## 実機の合格条件

1. 起動時に機種一覧が出て、先頭機種だけが選択色になる。
2. 上下の短押しで1項目ずつ移動し、元の項目は通常色へ戻る。
3. 長押しで、最初の待ち時間の後、一定間隔で移動を繰り返す。
4. ボタンを離すと停止する。先頭・末尾で止まる。上下同時押しで動かない。
5. CORES / OPTIONSまで移動できる。決定・戻るを押しても一覧のまま。
6. USBコントローラの接続・切断通知で、画面が従来表示へ戻らない。

## 検証結果

- ホストテスト：短押し、長押しの400/100 ms境界、離す操作、方向反転、上下同時押し、端点、空・1項目、タイマー周回、処理中断後の移動を確認。
- 全8選択位置で、glyphが変わらず選択色だけが移動することと、994 byteの送信内容・行単位critical sectionを確認。
- 実際のutils.cppのUART関数部分をホスト用SDK代替宣言でコンパイルし、従来表示→debug専用送信→従来表示復帰を確認。設定OFFも確認。
- AddressSanitizer / UndefinedBehaviorSanitizerで異常なし（環境制約によりLeakSanitizerは無効）。
- 既存のVerilator生成済みモデルを再利用し、全8選択位置について2 Mbps UART受信、全画素比較、独立clock、timeout後の復帰を確認。今回はRTL変更・モデル再生成なし。
- BL616向けcross buildと今回変更の実機試験は未実施。お手元で上記手順により確認する。

ホストテストの再実行：

```bash
bash tools/ui16/test_system_menu.sh
bash tools/ui16/test_navigation.sh
```

## 実機確認後のcommit

```bash
git add CMakeLists.txt main.cpp utils/utils.h utils/utils.cpp \
  ui/ui16_system_menu.h ui/ui16_system_menu.cpp \
  ui/ui16_navigation.h ui/ui16_navigation.cpp ui/ui16_display_mode.h \
  tests/ui16/utils.h tests/ui16/navigation_test.cpp tests/ui16/transport_owner_test.cpp \
  tools/ui16/test_navigation.sh docs/UI16_STAGE2.md
git diff --cached --stat
git commit -m "Add Up/Down navigation to UI16 system menu"
git push fork ui16-uart
```

`build/`、`build-ui16-host/`、適用用patchとscriptは追加しない。
親のtangcoreでもfirmware-bl616の参照更新をcommitする。
