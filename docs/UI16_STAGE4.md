# UI16 段階4：TSVタイトルと12 bit glyph

基準：firmware-bl616 `b8a3e2d1728c1fc2c8072a9064ea2b7099ce1b0e`、monitorcore `e472e929d63a3bda85a2e67f83c51d1aa79309fd`。
今回はFPGAとBL616の両方を更新する。旧8 bit版との混在は不可。

## 適用

ZIPのapply_ui16_stage4.py、firmware_stage4.patch、monitor_stage4.patchを親のtangcore直下にコピーする。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore
python apply_ui16_stage4.py
```

両方のpatchを先に検査し、変更対象ファイルに未commitの変更があれば中止する。
commit・push・microSDへのコピー・書込みは行わない。

## TSV

各ROMフォルダ直下のtitles.tsvをUTF-8で保存する。例：F:/nes/titles.tsv。
docs/examples/nes/titles.tsvが見本。filenameは実在するROM名へ変更する。

```tsv
filename	title
game001.nes	スーパーマリオブラザーズ
game002.nes	ゼルダの伝説
```

- 区切りは実際のtab。2列固定。カンマ・引用符・約物は通常の文字。
- BOMあり・なし、CRLF・LF、最終行の改行なしを許可する。
- filenameはASCIIのROMファイル名（拡張子を含む）。パスを書かない。大小文字を区別せず照合する。
- filename空欄・同じファイルへの重複登録はPC検査でエラー。
- titleが空欄・半角space/全角spaceのみ、または未登録ならファイル名を表示する。
- title内のtab・改行・C0/C1制御文字は禁止。filenameは255 byte、titleはUTF-8で512 byteまで。
- TSV全体は256 KiBまで。header以降の空行は許可する。
- titleの先頭/末尾spaceや同じ表示タイトルはPC側で警告し、自動修正しない。
- 元のROM名は別に保持する。FatFsのファイル名文字コード設定は変更しない。
- ROM起動・縦スクロール・複数行表示は後段階。今回は最初の10件、各titleは30半角単位まで。

## PC検査とフォント追加

親のtangcoreで実行する。Python標準libraryだけを使用する。

```bash
python firmware-bl616/tools/ui16/check_titles.py /f/nes/titles.tsv --core nes
python firmware-bl616/tools/ui16/check_titles.py /f/snes/titles.tsv --core snes
```

--coreはnes/snes/gba/genesis/sms/pc。形式不正、ROM不在、重複登録、未収録文字はエラー（終了code 1）。
未収録文字は文字・Unicode番号・TSV行番号を報告する。未登録ROMや空欄titleの代替ファイル名も文字検査する。
長いtitle、未登録ROM、空欄title、同じ表示title等は警告。

配布状態は既存glyph番号を維持し、ASCII記号・小文字と仮名・約物を加えた311文字（spaceを含む）。
ROM枠は1,024 glyph、生データ32 KiB。漢字等の追加は全機種のTSVをまとめて指定する。

```bash
python firmware-bl616/tools/ui16/build_font.py --monitor monitor \
  --tsv /f/nes/titles.tsv --tsv /f/snes/titles.tsv --capacity 1024
```

--tsvは必要なだけ追加可能。生成後にPC検査を再実行する。
東雲の元データを同梱しており、ネット接続は不要。元データにない文字（emoji、一部異体字等）は生成エラー。
既存番号は振り直さず、新しい文字だけを末尾へ追加する。同じ入力での再生成は再現可能。
容量不足なら--capacity 2048または4096を選べるが、実際のFPGAメモリ使用量はGowin合成で確認する。
番号は12 bit、最大4,096枠（0〜4095）。配布設定は1,024枠。

生成するもの：
- BL616：ui/ui16_font_map.h、ui16_config.h、ui16_font_manifest.json
- FPGA：src/iosys/ui16/font16_full.hex、font16_table.h、ui_config.svh、font_manifest.json

両manifestは同じ内容とSHA256を持つ。書き込んだbitstreamと対応するmanifestを保管する。
検査時に--font-manifestで保管したmanifestを指定できる。PC検査はFPGA実物の内容を直接読むものではない。
収録済み文字だけでtitleを変更した場合はTSV差し替えだけでよい。
新しい文字を追加した場合はFPGAとBL616を両方更新する。

## FPGAの合成・配置配線とmicroSDへのコピー

Gowin EDAでmonitor/monitor_console138k_ui_uart.gprjを開き、合成・配置配線を実行する。
固定表示用のui_testではなくUART版を使用する。

```bash
cp monitor/impl/pnr/monitor_console138k_ui_uart.bin /f/cores/console138k/monitor.bin
sha256sum monitor/impl/pnr/monitor_console138k_ui_uart.bin /f/cores/console138k/monitor.bin
```

monitor/src/iosys/ui16/font_manifest.jsonも、そのbinに対応する記録として保管する。
以前のgen_selftest.pyでフォントを再生成しない。段階4はbuild_font.pyを使う。

## BL616のbuild・書込み

```bash
cd firmware-bl616
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"
make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

DevCubeでbuild/build_out/tangcore_bl616.binを0x40000へ書き込む。
microSDのmonitor.binも更新した状態で電源を完全に切り、PCとのUSB接続を外し、電源アダプタからBOOTを押さずに起動する。
BL616がmicroSDの新しいmonitorをFPGAへ読み込む。

## 実機確認

1. 機種一覧からBでゲーム一覧へ入り、登録ROMが日本語titleで表示されることを確認。
2. title空欄・未登録では元のファイル名、小文字や`.`等の記号も表示されることを確認。
3. 約物、長いtitle、上下選択、Aで元の機種への復帰を確認。
4. TSVがない場合は通常のファイル名表示。不正なTSVや読込みエラーは全体をファイル名へ戻し、下部にTSV ERRORを表示。
5. 実機で未収録文字に遭遇した場合は□（ASCIIなら?）で示す。通常はPC検査で止める。

実機は最大10件の表示対象に対する重複を検出する。範囲外を含む全件の重複・ROM存在・収録文字検査はPC側が担当する。

## 通信・メモリ

段階4専用行commandは0x0f（旧版は0x0e）。1セル2 byte、全画面994 byteを維持する。
byte 0はglyph下位8 bit。byte 1はbits 5:2がglyph上位4 bit、bit 1が右半分指定、bit 0が選択色、bits 7:6は0。
セルRAMは14 bitでglyphがbits 11:0、選択/右半分がbits 12/13。
decoderは予約bit・ROM枠以上の番号を拒否し、その行を書き込まない。
読出しpipelineは3 hclk edgeのまま。

## 検証と保存

TSV逐次読込み、UTF-8/BOM/CRLF、空欄、引用符・約物、重複、不正UTF-8、読込み/closeエラー、サイズ上限、全体fallback、元ファイル名保持、12 bit送信、□代替のホストテストが成功。
PC検査、番号維持、再生成再現性、容量超過時に生成物を変更しないテストも成功。段階1〜3の回帰テストと固定3行senderも成功。
変更後RTLをVerilatorで再生成し、実際のBL616 senderの994 byteを2 Mbps UART波形で入力して全画素比較が成功。
1,024枠と4,096枠で最大番号の書込みを確認。予約bit・枠外番号の拒否、旧command無視、timeout/復帰、従来表示との切替を確認。
4,096枠はsimulationのみ。Gowin合成・配置配線、BL616向けcross build、今回変更の実機動作は未実施。

firmware-bl616でのテスト：
```bash
bash tools/ui16/test_titles.sh
bash tools/ui16/test_system_menu.sh
bash tools/ui16/test_navigation.sh
bash tools/ui16/test_browser.sh
bash tools/ui16/test_sender.sh
```

monitorはbash tools/ui16/test_hw.shとbash tools/ui16/test_integration.sh。
実機確認後、ZIPのCHANGED_FILES.txtの一覧で今回のソース・生成物・PC用フォント元bank・ライセンスをcommitする。
build/impl/obj_dir/__pycache__や適用patch/scriptは追加しない。
monitorとfirmware-bl616をそれぞれcommit/pushし、親tangcoreのsubmodule参照もcommitする。
