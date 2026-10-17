# 第5段階：一覧の縦スクロールと操作割当て

基準：firmware-bl616 `bc2f1f6d101b949c38e4c8881f640355c75ec37a`（ui16-uart）。
monitor配布基準：`a53f60783cd04548f4c535e0de95e00cf7b1b1ca`（ui16-uart）。
第4段階のtitles.csv（UTF-8・tab区切り）とフォルダ対応を維持する。

## 今回の変更

- 一覧の件数と画面内の10行を分離。画面は256×224、一覧は3〜12行目（0始まり）。
- 上下で1項目ずつ移動し、画面の上下端を越えたときだけ表示先頭を1項目ずつ動かす。ページ切替や画素単位の移動ではない。
- 先頭・末尾で停止し、一周しない。長押しは初回400ms、以後100ms。処理が遅れても1回の更新で1項目だけ動く。
- 機種一覧の10件制限も解除し、設定された全項目を表示・選択可能にする。
- 各フォルダから戻った際、入る前の選択項目と表示先頭を復元する。列挙順が変わった場合は元のフォルダ名を探し、選択項目が見える範囲に補正する。
- ROMルートから機種一覧へ戻ると、機種の選択と表示位置を保持する。機種へ入り直す場合、ROM一覧は先頭から開始する。
- A/Bのどちらでも決定、X/Yのどちらでも戻る。左下は「X/Y 戻る」。
- A+B、X+Yは各1回の操作。決定と戻るの同時押しは階層移動しない。決定・戻るの全ボタンを離すまで次の階層移動を受け付けない。
- CSVの形式・日本語名と内部名の分離・フォルダ分類・異常時の元名表示は従来どおり。

1フォルダの読込み上限はファイルとフォルダの合計256項目。256件目まで選択可能。
257件目以降がある場合は左下に「X/Y 戻る LIST LIMIT」と表示し、PC検査でも警告する。
この場合はフォルダを分割する。FatFsの列挙順を維持し、並べ替えは追加しない。
一覧をRAMに保持するための上限であり、画面内の10行とは独立している。

タイトルは従来どおり1行、半角30桁まで。2行表示は今回追加しない。
ROMの読込み・ゲーム起動と復帰は第6段階。ROMをA/Bで選んでも今回の版では起動しない。
CORES/OPTIONSも従来どおり未接続。

## フォントの更新

新しい案内の「戻」（U+623B）を追加し、glyph ID 318を割り当てる。
firmware側の既存318文字の番号・bitmapは変更しない。「通」も維持する。

公開monitorの基準には312文字、firmwareの基準には318文字が含まれていたため、
monitorにはfirmware側で既に使われている6文字（冒・名・実・版・遙・険）も同期する。
両側を同じ319文字／1024 slotのフォントにそろえる。
FPGA用bitmap・対応表・manifestとBL616用対応表・manifestは必ず同じ組合せを使う。

したがって今回は **monitorのFPGA再合成とBL616の再build・書込みの両方が必要**。
build_font.pyには案内文字の収録を必須化した。既存のCSVに文字を追加しなければ、適用後にフォントを再生成する必要はない。

## 適用

ZIPを親tangcoreに展開する。tangcore直下に以下が並ぶ構成にする。

- apply_ui16_stage5.py
- ui16_stage5_package/
- firmware-bl616/（既存）
- monitor/（既存）

まず適用前検査：

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore
python apply_ui16_stage5.py --check
```

成功後：

```bash
python apply_ui16_stage5.py
```

適用scriptは変更対象だけを検査する。未commit変更や想定外の内容があれば、書き換える前に中止する。
先に今回の第4段階の変更をcommitしておく。対象外のbuild、適用script、他submoduleの変更は触らない。
各ファイルはSHA256で照合し、CRLF/LFの違いは同一と扱う。出力はLF。
公開monitor基準の312文字版と、firmwareの既存318文字に同期した生成版を受け付ける。
適用前の対象ファイルはui16_stage5_backupsの日時別フォルダへ保存する。
中途適用・適用済みの再実行にも対応。Gitへのcommit/pushと実機への書込みは行わない。
同梱patchは差分確認用。適用には上記scriptを使用する。

## PC確認

```bash
cd firmware-bl616
bash tools/ui16/test_scroll.sh
bash tools/ui16/test_folders.sh
bash tools/ui16/test_titles.sh
bash tools/ui16/test_browser.sh
bash tools/ui16/test_navigation.sh
bash tools/ui16/test_system_menu.sh
bash tools/ui16/test_sender.sh
python tools/ui16/check_titles.py /f/snes --core snes
python tools/ui16/check_titles.py /f/nes --core nes
```

確認済み：137項目の上下往復、全表示行の内容・選択色・994 byte送信、
10/11件目の境界、256件目と件数超過、23機種、長押し、同時押し、
階層ごとの表示位置、列挙順変更後の復元、空/異常画面、CSV、フォントIDの維持。
AddressSanitizer/UndefinedBehaviorSanitizerでscroll_test成功（LeakSanitizerは無効）。
今回はRTLを変更していない。更新フォントを使ったRTL再simulation、Gowin合成、
BL616向けcross build、実機動作はこの環境では未確認。

## FPGA・BL616の更新

1. これまでと同じconsole138k用monitorプロジェクトをGowin EDAで開き、合成と配置配線をやり直す。
2. 従来と同じ生成・配置手順で、microSD上の対応するmonitor.binを更新する。
3. BL616を以下でbuildする。ツールの配置は第4段階と同じ。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"
make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

4. DevCubeでbuild/build_out/tangcore_bl616.binを **0x40000** へ書き込む。
5. 電源を完全に切り、PCとのUSBを外し、電源アダプタからBOOTを押さずに起動する。

## 実機確認

1. 11項目以上あるフォルダを開き、A/Bのそれぞれで決定できることを確認する。
2. 10行目からさらに下へ進むと一覧が1項目ずつ動く。上へ戻る場合も同様。
3. 下を長押しして末尾へ進み、停止する。上の長押しで先頭へ戻る。表示と選択色のずれがない。
4. スクロールした位置の子フォルダへ入り、XまたはYで戻る。選択項目と表示位置が元に戻る。
5. 多重のフォルダでも各階層の位置を復元する。
6. ROMルートからX/Yで機種一覧へ戻り、機種の選択を保持する。
7. 左下の「X/Y 戻る」、特に「戻」が正しい字形で表示される。
8. ボタンを押しっぱなしにしても階層移動を繰り返さない。空フォルダ・CSV異常からもX/Yで戻れる。
9. ROMをA/Bで選んでも起動しないことは第5段階の仕様。

## 実機確認後のGit保存

CHANGED_FILES.txtに対象を列挙した。適用用script、patch、バックアップ、build生成物は追加しない。
先にmonitor、次にfirmware-bl616、最後に親tangcoreの順で保存する。

```bash
git -C monitor add -- src/iosys/ui16/font16_full.hex \
  src/iosys/ui16/font16_table.h src/iosys/ui16/font_manifest.json
git -C monitor diff --cached --check
git -C monitor commit -m "Sync UI16 font and add Japanese Back label"
git -C monitor push fork ui16-uart
```

firmware-bl616の追加コマンドはCHANGED_FILES.txtの一覧に対応するadd_firmware_stage5.txtに記載。
追加後：

```bash
git -C firmware-bl616 diff --cached --check
git -C firmware-bl616 commit -m "Add UI16 vertical scrolling and A/B confirm X/Y back"
git -C firmware-bl616 push fork ui16-uart
git add -- monitor firmware-bl616
git diff --cached --submodule=short
git commit -m "Record verified UI16 stage 5 scrolling"
git push fork main
```
