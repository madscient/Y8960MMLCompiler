# Y8960 MML Compiler

MML を書いたテキストファイルから、
[Y8960 BASIC Extension](https://github.com/madscient/Y8960BasicExtension) の
`CALL MSAVE` が書き出すのと同じシーケンスデータ（`Y8SQ`）と、`CALL EXPORT PCM`
が書き出すのと同じ ADPCM ファイル（`Y8PC`）を作るクロスコンパイラ。
Windows / Linux / macOS 向けのコマンドラインツール。

**開発中。**

## 使い方

```
y8mmlc <MML ソース> [-o <基底名>] [--out-dir <フォルダ>]
```

```sh
y8mmlc song.mml
#  -> SONG.SQ   シーケンスデータ
#  -> SONG.PC   ADPCM のサンプルと設定（ソースに #pcmbank があるときだけ）
```

出力の名前は MSX-DOS の 8.3 に収まるように付く。`-o` で変えられる。

できたファイルは Y8960 BASIC Extension の `CALL MLOAD` と `CALL IMPORT PCM` が
読み、[Y8960Sequencer](https://github.com/madscient/Y8960Sequencer) が
そのまま鳴らせる。

### MML ソースの書き方

```
; コメントは行の先頭に置く

#assign A OPL2EX1 0
#assign B SCC 0
#assign C OPL2EX1 10

#define TEMPO 132
#define RIFF  "cdefgab>c<"

A  T=TEMPO; @6 V13 Q7 O4 L8 |: XRIFF; [1 >c4.< ] [2 >e4.< ] :|2
B  @1 V12 O2 L4 |: c g c g :|2
C  V10 @A14 |: B8 H8 S!8 H8 :|2
```

- 行の先頭が `#` ならメタコマンド、`A`-`P` ＋空白ならそのトラックの MML、
  `;` ならコメント。行末が `\` なら次の行がそこに続く
- MML のコマンドは Y8960 BASIC Extension と同じ
- 書き方の詳細は [`doc/mml-source.md`](doc/mml-source.md)

ADPCM を鳴らすには、[adpcm_packer](https://github.com/madscient/adpcm_packer) が
`adpcm-b` で出した `.json` と `.bin` を `#pcmbank` で読む。**バンクは自分で
番号を付ける**ので、エントリの並び順がそのまま `@0` `@1` … になる。

```
#pcmbank drums.json
```

番号を名前で固定したいときだけ `#adpcm` を足す。

```
#pcmbank drums.json
#adpcm   10 bassdrum
```

`@128`-`@191` の FM 音色と `@16`-`@31` の SCC 波形は `#voice` と `#wave` で
作る。長い行は末尾の `\` で折り返せる。

```
#voice 128 "Piano 1 ", \
           $00,$00,$0A,$00,$00,$00,$00,$00, \
           $31,$0E,$D9,$11,$30,$00,$00,$00, \
           $11,$00,$B2,$F4,$70,$00,$00,$00
```

## ビルド

CMake 3.20 以上と C++17 のコンパイラが要る。外部ライブラリは使わない。

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

| オプション | 既定 | |
|---|---|---|
| `Y8MMLC_BUILD_CLI` | ON | `y8mmlc` を作る |
| `Y8MMLC_BUILD_TESTS` | ON | 試験を作る |

## ドキュメント

| | |
|---|---|
| [`doc/mml-source.md`](doc/mml-source.md) | MML ソースファイルの書き方 |
| [`doc/plan.md`](doc/plan.md) | 設計判断、見送った案、未決事項、進捗 |

出力するデータの形そのものは Y8960 BASIC Extension の
[`doc/bytecode.md`](https://github.com/madscient/Y8960BasicExtension/blob/main/doc/bytecode.md)
と
[`doc/pcmfile.md`](https://github.com/madscient/Y8960BasicExtension/blob/main/doc/pcmfile.md)
が正。

## ライセンス

[MIT License](LICENSE)。

## 権利

`src/` のコードはこのプロジェクトが新規に書いたもので、MML とデータ形式の
**仕様**を流用したのであってコードではない。**例外は音色データ1本。**

`src/core/voicedata.cpp` のプリセット FM 音色64本は
[Y8960 BASIC Extension](https://github.com/madscient/Y8960BasicExtension) の
`src/tab/voicedat.asm` から機械的に写したもので、そちらは
[MSX-AUDIO BASIC Extension Lite](https://github.com/madscient/MsxAudioBasicExtensionLite)
の `src/vocdat.mac` の写し、さらにそちらは日本楽器製造株式会社（YAMAHA）および
株式会社アスキーの著作物をフォークしたもの。レコードの形も中身も変えていない。

**同じファイルのリズム音色3本だけは出典が異なる。** OPLL の ROM リズム音色を
Y8950 のレジスタへ変換したもので、元データは下記による。
**再配布時はこの出典を明記すること。**

> "Copyright free OPLL(x) ROM patches"
> <https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches>
> David Viens, Hubert Lamontagne 作, CC BY-SA

同じファイルの SCC プリセット波形16本は Y8960 BASIC Extension が生成したもので、
上記のいずれにも当たらない。

コンパイラがこの表を持つのは、シーケンスデータの仕様が「レコードのある音色は
シーケンスが自分で持つ」と定めているため。`@0`-`@63` を書いた曲のデータには、
その音色のレコードが埋め込まれる。

## 情報リソース

| | |
|---|---|
| Y8960 BASIC Extension | <https://github.com/madscient/Y8960BasicExtension> |
| Y8960Sequencer | <https://github.com/madscient/Y8960Sequencer> |
| adpcm_packer | <https://github.com/madscient/adpcm_packer> |
