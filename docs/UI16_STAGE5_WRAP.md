# 第5段階追加修正：折り返しと強制改行

基準：firmware-bl616 ui16-uart、commit `8823411`（第5段階縦スクロール・A/B決定・X/Y戻る）。
第5段階の不足していた最大2行表示を実装する。フォント・UART・RTLは変更しない。

## 表示規則

- 機種名、ROMタイトル、フォルダ名は最大2行。
- 1行目の本文は半角30桁。2行目は同じ開始位置から半角1桁（8 pixel）字下げし、本文を半角29桁まで表示する。
- CSVへ字下げ用のスペースを入れる必要はない。既に入っているスペースは本文として保持する。
- ASCIIは半角1桁、それ以外は2桁として数える（従来のUI16と同じ幅規則）。
- 強制改行がなければ文字単位で自動折り返しする。UTF-8の途中や全角文字の左右で分断しない。禁則処理・単語単位の折り返しは今回追加しない。
- 2行に収まらない自動折り返しのタイトルは、2行目末尾で切り詰め、PC検査で警告する。
- 2行の項目は両行を選択色で表示する。2行目先頭の字下げ部分も選択状態に含める。
- フォルダ末尾の/も本文の幅として数える。表示幅ぎりぎりの場合、/が2行目に送られることがある。
- 選択は項目単位。一覧の表示範囲は10行。選択項目の全行を画面内に収めるよう先頭項目を調整する。
- 画面下端に2行の項目が収まらないときは、その項目を途中まで表示せず空行を残す。
- 親へ戻る際は、入る前の選択と表示先頭を復元する。列挙順や件数が変わった場合は選択項目が見えるよう補正する。
- フォルダに入ったときの画面見出しは従来どおり1行。強制改行がある名称は1行目を見出しに使う。

## titles.csvの記法

ファイル形式はUTF-8・厳密なtab区切りのまま。列はnameとtitle。
以下の\nはバックスラッシュと英小文字nの2文字であり、ファイルの実改行ではない。

```text
name	title
game001.nes	ゼルダの伝説\n神々のトライフォース
action	アクション\nゲーム集
```

表示例（2行目の半角スペースは表示側で追加）：

```text
ゼルダの伝説
 神々のトライフォース
```

- `\n`は最大1個。指定位置で必ず改行する。
- 先頭・末尾、または半角/全角スペースだけの行を作る改行は形式エラー。
- 強制改行指定時は1行目30桁・2行目29桁まで。どちらかが超過したらエラー。さらに自動改行して3行にすることはない。
- `\\`はバックスラッシュ1文字を表示する。例えば`A\\nB`は改行せず、`A\nB`と表示する。
- `\t`、`\r`など未定義の記法や、末尾の単独バックスラッシュは形式エラー。
- 実際のtab/改行をtitle欄内に入れることはできない。通常のCSVの引用符による複数行記法も使わない。
- 記法の解釈はtitle欄のみ。ROM・フォルダの実名は変更しない。
- 自動折り返しではCSVの変更は不要。任意の位置で分割したいtitle欄にだけ\nを入れる。

実機のCSV解析とPC検査は同じ規則を使う。不正なCSVは従来どおりフォルダ全体を元の名前表示へ戻し、CSV ERRORを表示する。
フォント生成は記法を解釈し、改行制御を収録文字に含めない。
旧TSVからの移行は記法をファイル内の2文字のまま維持し、実改行へ変換しない。

## 適用

ZIPを親tangcore直下に展開する。既存のfirmware-bl616と並んで、
apply_ui16_stage5_wrap.pyとui16_stage5_wrap_packageを置く。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore
python apply_ui16_stage5_wrap.py --check
python apply_ui16_stage5_wrap.py
```

全対象を事前照合し、未commitの対象変更・想定外の内容があれば書込み前に中止する。
前回の第5段階をcommitした状態を基準とする。対象外の変更には触れない。
CRLF/LFの差を許容し、出力はUTF-8・LF。適用前の原本はui16_stage5_wrap_backupsに保存する。
commit/push・実機書込みは行わない。適用済みの再実行は変更しない。

## PC確認

```bash
cd firmware-bl616
bash tools/ui16/test_wrap.sh
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

Pythonのファイル入出力にUTF-8を明示し、ホストテスト用shell scriptからもUTF-8モードを指定した。
従来のCP932によるテスト用絵文字の書出し失敗に対応。test_scroll.shの末尾空行も除去済み。
フォント生成物もUTF-8・LFで出力し、CRLFによる行末空白の指摘を避ける。

確認項目：30/29桁の境界、全角文字の境界、強制改行とバックスラッシュ、
空行/未知記法/幅超過の拒否、フォルダの/、両行の選択色、字下げ、
画面下端の部分表示防止、137項目の混在と上下往復、親へ戻る際の復元、
機種一覧の2行表示、994 byteの送信内容。
PythonとC++の配置結果を、固定例と再現可能な乱数例191件で照合する。
既存ホストテストも回帰検証する。実機向けcross buildと実機動作はお手元で確認する。

## build・書込み

今回の追加修正だけならBL616側のみ更新する。前回の第5段階で追加した「戻」を含むmonitor.binを使う。
新しいタイトルに未収録文字を追加した場合は、従来どおりフォント生成とmonitor更新も必要。

```bash
cd /d/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616
compiler_bin="$(cygpath -m ../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin)"
export PATH="$PWD/../xuantie-gcc-riscv-v2.6.1-windows-x86_64/bin:$PATH"
export BL_SDK_BASE="$(cygpath -m ../bouffalo_sdk)"
make TANG_BOARD=console138k UI16_SYSTEM_MENU=ON UI16_UART_SELFTEST=OFF \
  cmake_generator='"MSYS Makefiles"' \
  CROSS_COMPILE="$compiler_bin/riscv64-unknown-elf-"
```

DevCubeでbuild/build_out/tangcore_bl616.binを **0x40000** へ書き込む。

## 実機確認・保存

1. 長い名称が自動で2行になり、2行目だけ半角1桁字下げされる。
2. title欄の\nを指定した位置で改行し、両行の選択色がそろう。
3. 1行と2行が混在する一覧で、下へ進んでも選択項目の2行目が欠けない。
4. 上下の長押し、先頭・末尾、子フォルダからX/Yで戻った際の位置を確認する。
5. A/B決定、X/Y戻る、左下の「X/Y 戻る」は維持される。
6. ROM起動への接続は第6段階で行う。

確認後はZIP内add_firmware_stage5_wrap.txtのgit addを実行し、次で保存する。

```bash
git -C firmware-bl616 diff --cached --check
git -C firmware-bl616 commit -m "Wrap UI16 titles with explicit breaks and indented continuation"
git -C firmware-bl616 push fork ui16-uart
git add -- firmware-bl616
git diff --cached --submodule=short
git commit -m "Record verified UI16 two-line menu layout"
git push fork main
```

適用script、patch、バックアップ、build/obj_dir/__pycache__は追加しない。

## 第6段階適用後

ROM起動・ゲーム内OSD・UI16一覧への復帰はUI16_STAGE6.mdを参照。
第6段階ではROMをA/Bで決定して起動できる。
