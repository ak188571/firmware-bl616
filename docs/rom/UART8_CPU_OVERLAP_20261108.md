# UART8 送信中のCPU処理と実機比較

2026年11月8日、日本時間。Console138K・BL616・SNES。

## 実装

8 Mbit/s、ROMXFR1 version 1、Window=2、最大1024 byte DATAを維持する。
共通Sessionへ非blocking `Transport::poll` を追加し、送信中のpumpから
ACK回収・CRC確認・次frame準備・frame CRC・stream CRCを進める。
UART固有のRX ring、DMA、baud設定はadapterが担当する。
将来SPIでも共通Sessionと同じpoll契約を使える。SPI物理転送は未実装。

送信中の枠を固定し、ACKだけではbufferを再利用しない。
DMA／物理送信終了と連続した先頭ACKの確定後に解放する。
投機的stream CRCは枠ごとに保持し、ACK確定時だけ正式なCRCへ反映する。
最大2件の履歴、同一byte列による最大4回の試行、ENDの実SDRAM全ROM読戻し、
END成功後だけSTARTする条件を維持する。
poll未実装のTransportでは従来のwindow管理を使う。

`TC_ROM_CPU_PIPELINE`は既定ON。詳細計測は既定OFF。
軽量計測時はV4へsend callback区間中の作業件数を記録する。
物理DMA busyのedgeや重なった時間を直接計測する値ではない。

## 実機結果

同一microSD・ROM・coreで、詳細計測OFFの前回対照版と各ROM各条件1回を比較。
DATA区間はRomBegin→RomQueued（全DATA ACK確定、開始交渉を含む）。
END区間はRomQueued→END ACK。人が見たタイトル到達時刻ではない。

| ROM | 前回DATA | 今回DATA | 短縮 | 前回END | 今回END |
|---|---:|---:|---:|---:|---:|
| 2 MiB | 3.286302 s | 2.723655 s | 17.12% | 0.487776 s | 0.487768 s |
| 4 MiB | 6.564648 s | 5.432306 s | 17.25% | 0.975429 s | 0.975417 s |

両方8 Mbit/s、window=2、再送0回。stream CRCはc287a01c／21e24729で、
保存した元ROMと64 byte headerからPython zlibで独立計算した値に一致。
send中の次frame準備は1975／3978件、ACK確定は2048／4096件、
stream CRC計算は1975／3978件。新記録はuart-0025.txt／uart-0026.txt、
対照記録はuart-0023.txt／uart-0024.txt。
利用者が2本とも起動・下段表示・一覧復帰の正常を確認した。
各1回の比較で、ばらつき・長時間の信頼性は確定しない。

## 検証・保全

実UART adapter、共通Sessionと受信RTL、UART信号、UART制御なしの共用部分を検証。
2／4 MiB、CRC破損、ACK欠落・遅延・分割・順序違い、早いACKでも送信buffer不変、
全ACK欠落時の最大4回打切り、SD読み出し・DMA失敗、SDRAM停止・読戻し破損を含む。
2→8→2 Mbit/s、送信bit周期±2%、実50 ms／10秒期限とwatchdogの試験も合格。
通常版・軽量計測版のBL616 buildに成功。FPGA runtime RTLの変更・再buildは行わない。

適用前にmicroSDの89 user filesを保存・SHA-256・属性・mtime照合。
全flash 4 MiBを2回読出し全byte・chip ID一致、復元用256 KiBを保存。
0x40000～0x7ffffのみ更新し、USB BOOT再接続後の独立全flash読戻しで
firmwareと対象外領域の一致を確認。SDはファイル単位の保存で、sector imageではない。
実行後91ファイル、既存89ファイルの変更・削除なし。追加は新しい記録2本のみ。

## 成果物の識別

- 実機適用済み軽量計測版: 246240 byte、SHA-256 `3dbad29760326de14118b0517e06899b3880b466cf378d8247d7cc0a9be99811`
- 通常版（独立読戻し・実機確認済み）: 241472 byte、SHA-256 `fa0e48157191afb19be3ac7ecfbaf4ffd0d00cefcda9bf6294b6f5700c4ec0eb`
- 変更していないSNES core: SHA-256 `ca489a5fe2696716d58dc968854ed1bfb75d67b37365b68b5c3f1f88e218e191`
- 現在の全flash: SHA-256 `634eacee88895021cf502e4d0a8af51d8427c217138adbc4b8154e645178a158`

作業・証拠は親workspaceの `uart8-cpu-overlap-20261108/`、報告書・配布物は
`outputs/uart8-cpu-overlap-20261108/`。現在は通常版（軽量計測・詳細計測ともOFF）を保持し、microSDはPC側。公開の到達点は以下に記録する。

## 通常版の実機確認・公開（2026年11月8日）

共通tag `uart8-cpu-overlap-hw-20261108`、統合先 `tangcore-integrated`。
軽量計測と詳細計測をともにOFFにした通常版を適用し、BOOT再接続後の独立した全flash読戻しで
firmwareと対象外領域の一致を確認した。利用者は2 MiB・4 MiBの2本とも
起動・下段の準備／転送／照合表示・一覧復帰を正常と確認した。
通常版の転送時間・タイトル到達時間は再計時していない。

- 通常版firmware: 241472 byte、SHA-256 `fa0e48157191afb19be3ac7ecfbaf4ffd0d00cefcda9bf6294b6f5700c4ec0eb`。
- 通常版全flash SHA-256: `7b0e1a58807062ae3e786cefe11da94395fefdda8f3a81538bce35f29ba432f5`。
- SNES core: 4767636 byte、SHA-256 `ca489a5fe2696716d58dc968854ed1bfb75d67b37365b68b5c3f1f88e218e191`。通常版切替時に変更なし。
- 切替前のmicroSD 91 user filesを保存・照合し、実行後も全ファイルの内容・属性・mtimeが一致。sector imageではない。
- 軽量計測版での改善: 2 MiB 3.286302→2.723655秒（17.12%）、4 MiB 6.564648→5.432306秒（17.25%）。各条件1回。
- ENDは0.487768／0.975417秒、CRCはc287a01c／21e24729、再送は両方0回。
- SPIの共用C++／RTLは検証済み、物理SPIは未実装。

親repositoryの `docs/rom/UART8_CPU_OVERLAP_20261108.md` と
`verification/uart8-cpu-overlap-20261108/` に配布物・対応commit・確認結果を記録する。
flash全体・SDコピー・ゲームROMは公開物へ含めない。
