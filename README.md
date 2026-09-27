# Y8960 MML Compiler

MML を書いたテキストファイルから、
[Y8960 BASIC Extension](https://github.com/madscient/MsxSoundSuiteExtension) の
`CALL MSAVE` が書き出すのと同じシーケンスデータ（`Y8SQ`）と、`CALL EXPORT PCM`
が書き出すのと同じ ADPCM ファイル（`Y8PC`）を作るクロスコンパイラ。
Windows / Linux / macOS 向けのコマンドラインツール。

**開発中。**

## 入手

[Releases](https://github.com/madscient/Y8960MMLCompiler/releases) から、
Windows（x64）と Linux（x64）の実行ファイルを入手できる。展開した `y8mmlc` を
そのまま使う。ほかの環境では、下の「ビルド」の手順でソースから作る。

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
- MML のコマンドは Y8960 BASIC Extension と同じ。一覧は [`doc/mml-reference.md`](doc/mml-reference.md)
- ファイルの書き方の詳細は [`doc/mml-source.md`](doc/mml-source.md)

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

`y8mmlc` が書き出した `.PC` も `#pcmbank` で読める。そのときは `.PC` が持つ番号が
そのまま使われる。

### シーケンスデータから MML に戻す

```
y8mmld <シーケンスデータ> [-o <ファイル名>] [--out-dir <フォルダ>] [--pcm <Y8PC>] [--force]
```

```sh
y8mmld SONG.SQ
#  -> SONG.mml
```

`.SQ` を、同じ鳴り方をする MML ソースに戻す。

- **元の書き方には戻らない。** マクロ・`L`・連符・コメントはシーケンスデータに
  残らないので、音符ごとに長さを書いた MML になる。音色と波形は `#voice` と
  `#wave` の定義として書き出され、`@128` と `@16` から順に番号が付く
- **MML の長さで書けない音符は、タイと休符に分けて書く。** `Q` で音を切っている
  音符は、鳴っている部分を `Q8` のタイでつなぎ、残りを休符にする。鳴り方は
  変わらない
- 書けないものがあれば警告を出し、そこを省く（時間を取るものは休符にする）。
  1 tick の音符は 2 tick にして、次の音から 1 tick 引く
- 戻した MML を `y8mmlc` に通し、それをまた `y8mmld` に通すと、同じ MML になる
- **ADPCM を鳴らすシーケンスデータには Y8PC が要る。** 同じ名前の `.PC` が隣に
  あればそれを使い、MML に `#pcmbank SONG.PC` と書く。別の場所にあるなら
  `--pcm` で指す
- **書き出す先に同じ名前のファイルがあれば、何も書かずに止まる。** 手で書いた
  `song.mml` を消さないため。上書きするときは `--force` を付ける

### ADPCM プリセット

`presets/` に ADPCM のサンプル 36 本を収めたバンクがある。そのまま
`#pcmbank` で読める。番号は JSON の `entries` の並び順で、`@0` から `@35`。

```
#pcmbank presets/wavs_y8950_adpcmb_excerpt.json
```

`@128`-`@191` の FM 音色と `@16`-`@31` の SCC 波形は `#voice` と `#wave` で、
SSGS・SCC・DCSG のソフトウェアエンベロープ `@E1`-`@E31` は `#env` で作る。
長い行は末尾の `\` で折り返せる。

```
#voice 128 "Piano 1 ", \
           $00,$00,$0A,$00,$00,$00,$00,$00, \
           $31,$0E,$D9,$11,$30,$00,$00,$00, \
           $11,$00,$B2,$F4,$70,$00,$00,$00
```

## ビルド

CMake 3.18 以上と C++17 のコンパイラが要る。外部ライブラリは使わない。

```sh
cmake -S . -B build
cmake --build build --config Release
cd build && ctest -C Release --output-on-failure
```

| オプション | 既定 | |
|---|---|---|
| `Y8MMLC_BUILD_CLI` | ON | `y8mmlc` と `y8mmld` を作る |
| `Y8MMLC_BUILD_TESTS` | ON | 試験を作る |

## ドキュメント

| | |
|---|---|
| [`doc/mml-reference.md`](doc/mml-reference.md) | MML のコマンドとその意味 |
| [`doc/mml-source.md`](doc/mml-source.md) | MML ソースファイルの書き方 |
| [`syntaxes/y8960mml.tmLanguage.json`](syntaxes/y8960mml.tmLanguage.json) | 構文強調の TextMate 文法（`scopeName` は `source.y8960mml`）。リズム音用 MML のトラックも普通の MML として色が付く |

出力するデータの形そのものは、[MSX Sound Suite Extension](https://github.com/madscient/MsxSoundSuiteExtension) に
収められた Y8960 BASIC Extension の文書
[`bytecode.md`](https://github.com/madscient/MsxSoundSuiteExtension/blob/master/docs/y8960/bytecode.md)
と
[`pcmfile.md`](https://github.com/madscient/MsxSoundSuiteExtension/blob/master/docs/y8960/pcmfile.md)
が正。

## ライセンス

[MIT License](LICENSE)。

## 権利

`src/` のコードはこのプロジェクトが新規に書いたもので、MML とデータ形式の
**仕様**を流用したのであってコードではない。**例外は音色データ1本。**

`src/core/voicedata.cpp` のプリセット FM 音色64本は
Y8960 BASIC Extension の `src/tab/voicedat.asm` から機械的に写したもので、
そちらは MSX-AUDIO BASIC Extension Lite の `src/vocdat.mac` の写し、さらに
そちらは日本楽器製造株式会社（YAMAHA）および株式会社アスキーの著作物を
フォークしたもの。レコードの形も中身も変えていない。Y8960 BASIC Extension と
MSX-AUDIO BASIC Extension Lite は、どちらも
[MSX Sound Suite Extension](https://github.com/madscient/MsxSoundSuiteExtension) に収められている。

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

`presets/` の ADPCM サンプルは、このプロジェクトの作者が自分で録音したもの。
ADPCM プリセットとして配布する。

## 情報リソース

| | |
|---|---|
| MSX Sound Suite Extension（Y8960 BASIC Extension を含む） | <https://github.com/madscient/MsxSoundSuiteExtension> |
| Y8960Sequencer | <https://github.com/madscient/Y8960Sequencer> |
| adpcm_packer | <https://github.com/madscient/adpcm_packer> |
