# フォント元データ

東雲16 dot、次の固定commitから取得。

https://github.com/code4fukui/shinonome-font/tree/053b21e0a11ef5799c1ea3cefc374763add80617

- `16/latin1/font_src.bit`（8×16）
- `16/kanjic/font_src.bit`（16×16、JIS X 0208。EUC-JP経由でUnicodeへ対応）

`shinonome16.json.gz` は上記をUnicode番号→16行のbitmapへ変換したもの。
各行は16 bit、半角文字は左8 bitを使用する。Python標準libraryのみで読める。
元のLICENSEとAUTHORSを `LICENSE_SHINONOME` / `AUTHORS_SHINONOME` として同梱。
元データ中のライセンス表記はPublic Domain。既存のglyph番号・bitmapは初回manifestへ引き継ぐ。

実機用生成物は `ui/ui16_font_manifest.json` を基準とし、`build_font.py` で新しい文字だけを末尾へ追加する。
一度追加された文字を削除したり、既存番号を振り直したりしない。
