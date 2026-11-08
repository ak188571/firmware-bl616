# ROMXFR1 — UART 8 Mbit/sと共通ROM転送

Console138KのSNESを最初の対象とする。CRC・ACK・再送・session状態は通信方式から分離し、将来のSPIでも同じmessageを使う。SPIのpin、clock、DMAやMISO読み出し方法はまだ実装していない。

## 構成

| 層 | firmware | FPGA |
|---|---|---|
| 共通message・CRC・再送 | `utils/tc_rom_protocol.*` | `src/rom/tc_rom_receiver.sv` |
| UART専用 | `utils/tc_rom_uart.*` | `tc_uart_rom_link.sv`、`tc_uart_phy.sv` |
| ROM供給・格納 | FatFsの4 KiB×2 buffer、SNES header | `tc_snes_rom_sink.sv`、既存SDRAM controller |

BL616のUART clockを160 MHzとし、2／8 Mbit/sを整数分周する。FPGAは既存PLLの未使用出力を80 MHzで有効にし、40／10 clockを1 bitとする。PLLの1200 MHz VCOと既存のUSB用96／12 MHz、SNESのmclk、HDMI設定は維持する。UART制御をUSBの96 MHz domainから分け、広い比較結果はregisterへ保存してから使う。

UART TXは`dma0_ch3`を専用使用し、4 KiB×2の供給buffer、最大1048 byteのframe、LLIを64 byte境界のnoncache RAMへ置く。同じDMA channelを他の処理から再設定しない。将来SPIを追加する場合のDMA channel割り当ては、そのadapter側で別途管理する。

`TC_ROM_UART=ON`は`UI16_SYSTEM_MENU=ON`かつ`TANG_BOARD=console138k`に限定する。Makefileでも同じoptionを指定できる。defaultはOFF。FPGAが`ROMXFR1`と`UART8M1`を両方通知したときだけ新経路を使う。未対応coreは従来の2 Mbit/s経路を使う。新経路を開始した後の失敗では、従来のloading=0や未照合の起動へ移行しない。

## Message形式

多byte整数はbig endian。message全体は24+N byte、Nは0～1024。

| offset | byte数 | 内容 |
|---:|---:|---|
| 0 | 1 | AA |
| 1 | 2 | length=21+N（この3 byteを除く長さ） |
| 3 | 1 | request=30、reply=31（hex） |
| 4 | 1 | version=1 |
| 5 | 1 | operation |
| 6 | 4 | session ID（0禁止） |
| 10 | 4 | DATA sequence |
| 14 | 4 | stream内の絶対offset |
| 18 | 2 | N |
| 20 | N | payload |
| 20+N | 4 | CRC-32/ISO-HDLC |

CRCはAAからpayload末尾までを含む。反転polynomialはEDB88320、初期値FFFFFFFF、最終XORはFFFFFFFF、`123456789`の値はCBF43926。replyも同じ形式で、payloadは1 byteのstatus、offsetはcommit済みの次の位置を返す。

| operation | 値 | payload・条件 |
|---|---:|---|
| BEGIN | 1 | stream総byte数32 bit、sequence=offset=0 |
| DATA | 2 | 1～1024 byte、sequenceとoffsetが次の位置と一致 |
| END | 3 | stream全体のCRC32。全DATAのcommit後のみ |
| START | 4 | payloadなし。END照合済みの場合のみ |
| ABORT | 5 | payloadなし、sequence=offset=0。reset保持 |
| LinkRate | 16 | UART専用のbaud値32 bit |
| LinkSync | 17 | UART専用の切替後確認、payloadなし |

statusは0=OK、1=format、2=session、3=order、4=range、5=state、6=busy予約、7=integrity、8=backend。CRC不一致のframeは実行せず、replyを返さない。

各requestは初回を含め最大4回。通常50 ms、ENDは実SDRAM読み戻しを含むため10秒。stop-and-waitで、再送では同じsession／sequence／offset／payloadを保持する。受信側は直前のDATAを別RAM bankに残し、byte単位で一致する重複だけをACKする。重複では書き込み、offset、stream CRCを進めない。

## UARTと安全な開始

1. 2 Mbit/sでBEGINし、ACKを確認する。
2. LinkRate=8000000のACKを旧速度で送信完了してから、FPGAは4 ms待って切り替える。BL616はACK後6 ms待ち、8 Mbit/sにしてLinkSyncする。
3. DATAを転送する。DMA中に次の4 KiBを先読みし、FatFsを別taskから呼ばない。RX ISR ringはROM転送側が単独で消費し、通常のUART parserを停止する。SDKのUART1 printf出力も除外する。
4. ENDでは受信stream CRCに加え、実際のSDRAMを全ROM分読み戻す。headerの受信CRCと読み戻したROMのCRCが一致してからverifiedにする。
5. 同じ手順で2 Mbit/sへ戻し、LinkSync後にSTARTする。従来のOSD／入力通信は2 Mbit/sで続ける。

DATAのcommitはSDRAM requestのACKと、その後4 mclkの待ちを経て通知する。通信無活動のwatchdogは通常1秒とする。END照合済みの状態だけは12秒とし、送信側がACK消失による10秒timeoutを待った後も同じENDを再送できるようにする。いずれも期限後はUARTを2 Mbit/sへ戻し、未起動ゲームのresetを保持する。backend処理中はidle watchdogを進めず、別の12秒の処理期限を用いる。SDRAMのACK待ちは10 msで失敗、未完了backendは再利用せずresetを要求する。送信側も失敗時はDMAを止め、2 Mbit/sへ戻して通常1.2秒、ENDを試行した後は12.2秒待つ。呼び出し元はmonitorを復元する。

SNESのstreamは64 byte headerとROM本体を連結する。ROMは偶数byte、最大0x7e0000 byteとし、WRAM領域への侵入を拒否する。FPGAには1024 byte×2のRAM bank、受信descriptorの保持とACK、clock domain間の2段同期を置く。全clock間を一括でfalse pathにせず、追加の例外は同期第1段、保持descriptorのcapture、reset同期回路の非同期CLEAR端子に限定する。resetの解除は2段で同期し、その段間のD入力への経路は解析対象に残す。

通常のSNES送信は64 byte headerの後に偶数byteのROM blockを送り、DATAごとに全wordの書き込みを待つ。共通層の試験では奇数byteの分割も扱う。この場合だけはword途中の末尾1 byteをSNES adapterに保持してACKし、次のDATAでwordを書き込む。保持したbyteも含めて再送を二重実行せず、ENDでは端数が残っていないことと、全ROMの実SDRAM照合を要求する。

## SPIへ追加する場合

`TcRom::Transport::exchange`だけをSPI用に実装する。requestを送った後のreply取得、DMA完了・chip select・timeoutはそのadapterが担当する。`Session`のCRC、4回の試行、sequence／offset確認、END／START状態は共通のまま使う。FatFsの供給部も再利用できる。

FPGAもbyte入出力を`tc_rom_receiver`へ接続する。`LINK_CONTROL=0`、`link_at_base=1`とし、LinkRate／LinkSyncを使わない。SPI adapterは完全なrequestを送り終え、backend処理とreply完了を待って次のrequestを送る。`rx_valid`間には少なくとも1 idle clockを置き、replyを`tx_valid/tx_ready`で回収する。`tx_idle`は物理送信完了後に通知する。SPI側と処理側のclock domainが異なる場合も、共有状態を直接参照せずFIFOまたは保持・ACKによる接続を用いる。

BEGINのACKをすべて失うなど受信状態を確定できない失敗では、SPI adapterが`transport_fault`／resetを通知し、未照合ゲームのresetを保持する。backendが未完了ならresetを要求する。UART側のwatchdogをSPIの復旧手段として仮定しない。

## 検証と実機で残る事項

共通C++を実際の受信RTL／SNES sinkと接続し、2／4 MiB、CRC破損、ACK消失、完全な重複、異なる内容の重複、順序違い、SDRAM読み戻し破損・応答停止を試験する。UART信号試験では2→8→2 Mbit/s、連続8N1、送信bit周期±2%、切替ACK消失とwatchdogを確認する。DATA／ENDのACK消失後はそれぞれ50 ms／10秒の実timeoutを経てから再送し、照合済み状態で12秒経過した場合の復帰も確認する。

PC試験は実機の配線品質・電源・BL616割り込み遅延を確定しない。実機では新firmwareとSNES coreを対応する組で用い、旧flash・microSDの保存、読み戻し照合、2／4 MiBの起動・OSD復帰、再送回数と実時間の確認が必要。8 Mbit/sはline rateであり、SDRAM書き込み・ACK・CRC読み戻し・ゲーム初期化を含む速度が4倍になることは意味しない。

旧`TC_UART_BASELINE_V1`の2 Mbit/s記録を8 Mbit/sとして扱わない。通常成果物では`TC_LAUNCH_TRACE=OFF`。計測を有効にした新経路は`TC_ROMXFR1_TRACE_V2`としてbaud=8000000、request／retry数、stream CRC、END ACK時刻を記録する。新経路のRomQueuedはDATA ACK完了の境界で、旧経路のCPU投入終了とは意味が異なる。新経路のuart_enqueue_usはDMAの投入処理の累積であり、送信・ACK待ち時間ではない。


## 実機確認済みの到達点（2026年11月8日）

共通tagは `uart8-progress-hw-20261108`。Console138KのSNESでUART 8 Mbit/s、dma0_ch3による送信、CRC・再送とSDRAM読み戻し照合を確認した。
通常版はTC_LAUNCH_TRACE=OFFで、最下段のPreparing SNES core、ROM transfer 0%～100%（5%刻み）、Verifying ROMを表示する。
準備画面では旧画面の文字とロゴ用RAMを消去してから表示する。通常版での起動とロード中の表示は利用者が実機確認した。

通常版firmwareは237680 byte、SHA-256は `1f1bdb2455e99210b420a9e94bd20b2565db7506c1f79deb086fba87733403f2`。
対応するSNES coreのSHA-256は `e3b9565a05791420073f20d6d8ebd0b8d40b42eddfc05e04f4cdf6340540613f`。
実機記録・時間の比較・対応するcommitは親repositoryの `verification/uart8-progress-20261108/` と `docs/rom/UART8_PROGRESS_20261108.md` に記録する。
