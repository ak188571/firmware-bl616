# 段階4改訂：tab区切りのtitles.csvとフォルダ移動

基準：firmware-bl616のui16-uart、commit `7f6a2513e99bb9216eb5c53c8c44873a1cefd8f5`。
動作確認済みの12 bit glyph版を基準とする。今回のpatchはBL616側だけを変更し、追加済みの「通」を含むフォント・manifest・UART形式は維持する。

## 適用

ZIPのapply_ui16_csv_folders.pyとui16_csv_folders.patchをfirmware-bl616直下にコピーする。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
python apply_ui16_csv_folders.py
```

scriptは変更対象の未commit変更を保護し、git apply --check成功後だけ適用する。commit、push、microSDへの書込みは行わない。

## 新しいデータ形式

名前はtitles.csvだが、区切りは厳密にtab。カンマ区切りはエラー。
列名をfilenameからnameへ変更し、ファイルとフォルダを同じ表に登録する。

F:/nes/titles.csv：

```text
name	title
action	アクション
puzzle	パズル
root.nes	ルートにあるゲーム
```

F:/nes/action/titles.csv：

```text
name	title
xxx.nes	日本語のゲームタイトル
```

- nameには同じフォルダ直下の実物の名前だけを書く。action/xxx.nesのようなパスや末尾の/は書かない。
- ファイル・フォルダ名はASCII。大小文字を区別せず照合する。分類は実物の属性で判定する。
- フォルダにもtitleを付けられ、画面では区別用の/を末尾に表示する。
- 各フォルダで、そのフォルダ自身のtitles.csvを読む。親の対応表を子へ引き継がない。
- title空欄・半角space/全角spaceのみ、または未登録なら元の名前を表示する。
- カンマ・引用符・約物は通常の文字。引用符による囲みやescape処理はしない。
- UTF-8、BOMあり/なし、CRLF/LF、最終行の改行なしを許可する。
- 各行は2列。tab・改行を項目内に入れない。C0/C1制御文字も禁止。
- nameは255 byte、titleはUTF-8で512 byte、ファイル全体は256 KiBまで。
- 表計算ソフトからもUTF-8・tab区切りで書き出し、列を引用符で囲む出力は使わない。

titles.csvがなければ名前をそのまま表示する。不正・読込み失敗時は部分適用せず、そのフォルダ全体を元の名前表示へ戻し、CSV ERRORを表示する。
旧titles.tsvは実機では読まない。

## 既存titles.tsvの移行

親のtangcoreから、まず予定を表示する。

```bash
python firmware-bl616/tools/ui16/migrate_titles.py /f/snes
```

実際に新ファイルを作る場合：

```bash
python firmware-bl616/tools/ui16/migrate_titles.py /f/snes --apply
python firmware-bl616/tools/ui16/migrate_titles.py /f/nes --apply
```

配下を再帰的に調べ、titles.tsvを残したままtitles.csvを作る。headerだけfilenameからnameへ変え、tabとtitle本文を維持する。
BOMは除去するが新形式で許可される。CRLF/LFは維持する。
全対象の構文・競合を先に検査し、内容の異なる既存titles.csvは上書きせず中止する。
生成済みと同じ内容ならスキップする。既に手作業で新形式にした場合は移行処理は不要。
フォルダの日本語名の行は、移行後に親フォルダのtitles.csvへ追加する。

## 再帰検査

ROMルートフォルダを指定すると、その配下のCSV・ファイル・フォルダを検査する。

```bash
python firmware-bl616/tools/ui16/check_titles.py /f/snes --core snes
python firmware-bl616/tools/ui16/check_titles.py /f/nes --core nes
```

ルートのtitles.csvを指定しても、その親をROMルートとして再帰検査する。
存在、同一フォルダ内の重複登録、英字大小文字の曖昧さ、nameの形式、UTF-8、制御文字、未収録文字、表示幅を確認する。
フォルダの末尾に付ける/も表示幅に含める。
旧titles.tsvしかないフォルダは移行を促すエラー。新しいCSVがあれば残したTSVは無視する。
未登録や空欄titleは警告にとどめ、代替する元の名前もフォント検査する。
隠し/system属性の項目は対象外。symbolic linkは対象外としてエラーにする。

## 必要な文字の追加

フォント生成の引数も--tsvから--csvへ変更した。ROMルートを渡すと配下のtitles.csvをすべて集める。

```bash
python firmware-bl616/tools/ui16/build_font.py --monitor monitor \
  --csv /f/nes --csv /f/snes --capacity 1024
```

特定のtitles.csvファイルを--csvで指定することもできる。複数の--csvを指定可能。
その後に検査を再実行する。
**既存フォントでエラーがなければ、この生成処理とFPGA再合成は不要。**
新しい文字を生成した場合は、従来どおりmonitorを再合成してmicroSDのmonitor.binを更新し、BL616も同じ対応表でbuildする。
既存glyph番号と「通」は保持する。フォント更新の詳細はUI16_STAGE4.mdの該当節を参照するが、入力は新しいCSV仕様を使う。

## 操作と制限

- 機種一覧でB：その機種のROMルートへ入る。
- フォルダでB：子フォルダへ入る。見出しは親CSVに登録された日本語表示名になる。
- A：親フォルダへ戻り、入る前に選んだフォルダを復元する。ROMルートでAなら機種一覧へ戻る。
- 決定/戻るは押した瞬間だけ受け付け、長押しを次の階層へ持ち越さない。
- 空フォルダ・開けないフォルダからもAで戻れる。
- ROMファイルでB：この段階では起動しない。CORES/OPTIONSも未接続。
- 一覧は各階層の先頭10項目（ファイルとフォルダの合計）。列挙順はFatFsのままで、並べ替えは追加しない。
- 件数超過はMORE ITEMS。PC検査でも警告する。縦スクロールは次の段階。
- titleは行末で切り詰め、全角文字を分断しない。複数行表示は後段階。
- ROMルートから最大16階層、相対パス511 byteまで。超える移動はNAV ERRORで止め、親へ戻れる。PC検査も同じ上限を使う。

## build・書込み

既存の文字だけを使う場合、FPGAとmicroSDのmonitor.binは変更不要。BL616をbuildする。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"
make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

DevCubeでbuild/build_out/tangcore_bl616.binを0x40000へ書き込む。
電源を完全に切り、PCとのUSB接続を外し、電源アダプタからBOOTを押さずに起動する。

## 実機確認

1. 親CSVの登録でaction等のフォルダが日本語名＋/で表示される。
2. Bで子へ入り、そのフォルダのCSVでゲーム名が表示される。
3. さらに子へ入ってAで一段ずつ戻り、元のフォルダ選択が復元される。
4. 空フォルダ・存在しないフォルダ・不正なCSVでもAで戻れる。
5. B/Aを長押ししても階層を連続移動しない。
6. ROMルートでAを押すと、機種一覧の元の選択へ戻る。

## 検証・保存

実際のFatFs呼出し部分にホスト用代替を接続し、フォルダの分類、各階層のCSV、画面移動、戻り先の復元、列挙順変更、空/エラー、長押し、階層・パス上限を確認した。
PC側は再帰検査、対象フォルダの範囲、カンマ区切り拒否、末尾/やパスの拒否、重複、移行の事前検査・再実行を確認した。
再帰的フォント生成でも既存番号を保持するテストが成功。既存の段階1〜4テストも成功。
AddressSanitizer/UndefinedBehaviorSanitizerで異常なし（環境制約でLeakSanitizerは無効）。
変更していない段階4のRTLモデルへ、フォルダ表示の実際の994 byteをUART波形で入力し、全画素比較が成功した。
今回のBL616向けcross buildと実機動作は未確認。

```bash
bash tools/ui16/test_folders.sh
bash tools/ui16/test_titles.sh
bash tools/ui16/test_browser.sh
bash tools/ui16/test_navigation.sh
bash tools/ui16/test_system_menu.sh
bash tools/ui16/test_sender.sh
```

実機確認後、ZIPのCHANGED_FILES.txtのファイルをfirmware-bl616でcommit/pushし、親tangcoreの参照を更新する。
build/obj_dir/__pycache__、適用patch/scriptは追加しない。microSD上の実データは別途バックアップする。
