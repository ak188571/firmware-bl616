# ROMXFR1の時間内訳計測

UARTは8 Mbit/s、最大DATAは1024 byte、windowは2のまま。CRC-32/ISO-HDLCの式・対象byte、session、sequence、offset、最大4回の再送、同一DATAの保持、ENDの実SDRAM全ROM照合とSTART条件は変更しない。SNES coreはSDRAM連続格納版の実機確認済みbinaryをそのまま使用する。

## 計測の境界

`TC_ROM_PROFILE=ON`は`TC_LAUNCH_TRACE=ON`と`TC_ROM_UART=ON`を要求する。データはRAMだけへ記録し、従来どおりゲームから一覧へ戻りmonitorを復元してから新しいuart-NNNN.txtへ保存する。計測中にSDへlogを書いたりUARTへprintfしたりしない。

DATAは`Session::stream`の直前から直後、ENDは`Session::finish`の直前から直後を計測する。RomBegin→RomQueuedにはBEGIN・速度切替・Window確認とclock読出しの校正も含まれるので、DATAの内訳合計と同一視しない。RomQueued→EndAckはEND内訳よりわずかに広い境界である。

## 重複しない内訳

共通`TcRom::Profile`はbegin/endの通知だけを行い、clockと集計はfirmware側のobserverが担当する。共通codecとSessionへUARTのbaud、DMA、pin、SPIのCSを持ち込まない。observerはCRC判定や再送の結果を返さず、転送の制御条件を変更しない。

begin/endの通知で8段以内のstackを使い、直前の区間へ経過時間を加算してからcategoryを切り替える。入れ子のSD読み出しやCRCは親区間から除かれる。各phaseは次の11項目の合計がwall_usへ完全一致することを要求する。

| key | 測る処理 |
| --- | --- |
| cpu_us | 他へ分類しないCPU側処理、Session管理、pumpの周辺処理 |
| copy_us | Sourceのread callback。内部のSD読み出しは除く |
| frame_crc_us | encode内のrequest frame CRC計算 |
| stream_crc_us | 連続ACKによりcommitしたpayloadのstream CRC更新 |
| reply_crc_us | UART adapterと共通Sessionでのreply CRC検証 |
| sd_us | f_readの呼出し。filesystemやdriverを含む経過時間 |
| tx_setup_us | DMA descriptor・UART DMA設定・開始処理 |
| tx_wait_us | pump終了後のDMA完了確認と待ち |
| tx_drain_us | FIFOと物理送信の排出確認・待ち |
| ack_cpu_us | ACK byte収集・解析。待ちとCRCを除く |
| ack_wait_us | RX ringが空の期間。pollingと割込み等もこの期間へ入る |

いずれもsoftware境界で測る経過時間で、CPU命令の実行cycleだけを表す値ではない。割込み・RTOSによる中断と計測処理の負荷はその時点のcategoryへ入る。

## UART送信とSD読み出しの重なり

`tx_envelope_us`はDMA開始直前から、DMA終了と物理送信の排出を確認した直後までの合計。`tx_gap_us`は前の排出確認後から次のDMA開始直前までの合計。合計は最初の開始から最後の終了までの`tx_span_us`に一致する。これはsoftwareで観測した区間で、wire上のedgeを直接captureした値ではない。

`sd_in_send_us`は上のsend区間内で実行されたf_readの時間。SD読み出しが送信終了後まで延びる場合も含むため、全時間がwire送信と重なったと断定しない。これは`sd_us`と`tx_envelope_us`に含まれる補助値で、wallの内訳へ追加しない。

UART DATAのwire時間の下限は`tx_bytes × 10 / 8000000`秒（8N1）。softwareで観測したsend区間との差には開始・確認処理、DMA供給の停止、割込み・RTOSによる確認遅延などが含まれる可能性がある。差をそのままwireのidle時間と呼ばない。

## CRCとACKの検査

CRCの呼出し数と対象byte数を記録し、計測により対象を減らしていないことを検証する。再送なしのDATAではframe数はROM本体block数+headerの1件、stream CRC対象はROM本体+64 byteである。frame CRCはpayload+各20 byteを対象とし、送信byte数はpayload+各24 byteとなる。

通常のwindow=2ではACKをUART adapterとSessionでそれぞれ検証するため、reply CRCは各ACKで2回、各21 byte。ENDはexchange内の照合も含めて3回で、同じ検証を削除しない。再送時は保存済みの同一frameを使い、commit済みpayloadのstream CRCを重複更新しない。

`ack_ready`はreceive callbackの最初のRX ring取得でbyteが存在した回数。`ack_empty_polls`はringが空だった取得回数。すでに来ていたACKのCPU収集と、まだ来ないACKを待った時間を区別する。

## 計測の負荷と再現

開始前にclock読出し1000回の経過時間を測り、実計測のclock読出し数も保存する。この校正はDATA/ENDの内訳外。observer全体の負荷を完全に表すものではないため、この値だけを使って補正・差引きしない。

詳細計測ON/OFFのfirmwareを同じsource・compilerでbuildする。OFF版の実機測定を行っていない段階では、計測の負荷が実機で正確に何秒かかったかを確定しない。元の計測版との比較にはsourceやbuild配置の差もあるため、微小な差の原因を断定しない。

formatは`TC_ROMXFR1_PROFILE_V3`。既存V2項目にD_/E_の内訳を追加する。解析時にはformat、重複key、項目、単調な時刻、CRC、window/baud、要求数、全内訳の和、send envelope/gapの和、CRCの対象byte数、SD読み出しbyte数を検証する。`profile_error!=0`は内訳を不正として扱い、ゲーム起動の成否と区別する。
