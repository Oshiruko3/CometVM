# 📚 COMET VM 学習用サンプルコード集

COMET VM に同梱された、CASL II アセンブラを基本から応用（入出力、ループ、配列ソート、ビット操作、サブルーチン、スタック）まで学べる学習用サンプルプログラム集です。

各サンプルには**期待値**と**コードの解説**が付いています。サンプルを実行して期待値と照合しながら学習してください。

```bash
cd vm
make
./cometvm ../samples/sample01_hello_world_out.cas   # 1本を実行
make sample                                           # 全部を実行
```

---

## 📊 サンプル一覧

| ファイル名 | プログラム名 | 使われている主な命令・概念 | 期待値 |
| :--- | :--- | :--- | :--- |
| `sample01_hello_world_out.cas` | Hello World 出力 | `OUT`, `DC` | 画面に `HELLO, CASL II!` が表示される |
| `sample02_sum_1_to_n.cas` | 1〜N の総和計算 | `ADDA`, `SUBA`, `JNZ` | `GR0 = 5050` |
| `sample03_max_min_search.cas` | 配列の最大値・最小値探索 | 指標修飾 `ARRAY, GR2`, `CPA`, `JPL`, `JMI` | `GR0 = 98`（最大）・`GR1 = 3`（最小） |
| `sample04_bubble_sort.cas` | バブルソート（昇順） | 多重ループ, `CPA`, `JPL`, 交換 `ST` | `ARRAY` が `[11, 12, 22, 25, 34, 64, 90]` にソートされる |
| `sample05_string_length_count.cas` | ナル終端文字列の長さ | `CPA`（0判定）, 指標走査 | `GR0 = 12` |
| `sample06_case_conversion.cas` | 小文字→大文字変換 | ビットマスク `AND #FFDF`, 範囲チェック | `HELLO WORLD` が出力される |
| `sample07_factorial_subroutine.cas` | サブルーチンによる階乗 | `CALL`, `RET`, `RPUSH`, `RPOP` | `GR0 = 720`（6!） |
| `sample08_hex_to_decimal.cas` | 16進文字→数値変換 | 条件分岐, 文字コード演算 | `GR0 = 15`（'F' を変換） |
| `sample09_keyboard_input_echo.cas` | キーボード入力＆オウム返し | `IN`, `OUT`, `DS` | 入力した文字列が `You typed: ...` と表示される |
| `sample10_stack_push_pop_demo.cas` | スタック操作デモ | `PUSH`, `POP`, LIFO順序 | `GR1 = 30`・`GR2 = 20`・`GR3 = 10` |

---

## 📖 各サンプルの解説

### 01. Hello World 出力（`sample01_hello_world_out.cas`）

CASL II の最初の一歩。`OUT` マクロ命令で文字列を画面に出力します。

```cas
SAMPLE01 START
         OUT     MSG, LEN        ; MSG領域のLEN文字を出力
         RET                     ; プログラム終了
MSG      DC      'HELLO, CASL II!'
LEN      DC      15
         END
```

- **`OUT` マクロ**: `OUT 領域, 長さ` の形で、領域の先頭から指定文字数を出力します
- **`DC` 命令**: 定数をメモリに配置します。文字列は1文字1ワード（下位8bitに文字コード）
- **`RET`**: プログラムの終了（COMETではトップレベルのRETが終了を意味します）

**学べること**: OUTマクロ・DCによる文字列定義・RETによる終了

---

### 02. 1からNまでの総和（`sample02_sum_1_to_n.cas`）

カウンタループの基本形。1 + 2 + ... + 100 = 5050 を計算します。

```cas
SAMPLE02 START
         LD      GR1, N          ; カウンタ GR1 = 100
         LAD     GR0, 0          ; 合計 GR0 = 0
LOOP     ADDA    GR0, GR1        ; 合計にカウンタを加算
         SUBA    GR1, ONE        ; カウンタを1減らす
         JNZ     LOOP            ; 0でなければループ継続
         RET
N        DC      100
ONE      DC      1
         END
```

- **`LD`**: メモリの値をレジスタへロード（FRを更新します）
- **`LAD`**: アドレス値（即値）をレジスタへ
- **`JNZ`**: 直前の演算結果が0でなければ分岐（ZF=0で分岐）

**期待値**: `GR0 = 5050`（1〜100の総和）

**学べること**: カウンタループ・ADDA/SUBA・JNZによる条件分岐

---

### 03. 配列の最大値・最小値探索（`sample03_max_min_search.cas`）

指標レジスタを使った配列走査と、比較分岐の練習です。

```cas
SAMPLE03 START
         LAD     GR2, 0          ; インデックス GR2 = 0
         LD      GR3, LEN        ; 配列長 GR3 = 8
         LD      GR0, ARRAY, GR2 ; 最大値の初期値 = ARRAY[0]
         LD      GR1, ARRAY, GR2 ; 最小値の初期値 = ARRAY[0]
LOOP     LAD     GR2, 1, GR2     ; インデックス++
         CPA     GR2, GR3
         JZE     DONE            ; 末尾なら終了
         LD      GR4, ARRAY, GR2 ; 現在要素
         CPA     GR4, GR0
         JPL     SET_MAX         ; 最大値の更新
         CPA     GR4, GR1
         JMI     SET_MIN         ; 最小値の更新
         JUMP    LOOP
SET_MAX  LD      GR0, GR4
         JUMP    LOOP
SET_MIN  LD      GR1, GR4
         JUMP    LOOP
DONE     RET
ARRAY    DC      45, 12, 98, 3, 67, 89, 23, 54
LEN      DC      8
         END
```

- **指標修飾** `ARRAY, GR2`: 配列の先頭からGR2番目の要素を参照
- **`CPA`**: 算術比較（符号付き）。結果に応じてSF/ZFを設定
- **`JPL`**: 正（S=0かつZ=0）なら分岐 / **`JMI`**: 負（S=1）なら分岐

**期待値**: `GR0 = 98`（最大値）、`GR1 = 3`（最小値）

**学べること**: 指標修飾による配列走査・CPA比較・JPL/JMIの使い分け

---

### 04. バブルソート（`sample04_bubble_sort.cas`）

二重ループで隣接要素を比較・交換し、配列を昇順に並べ替えます。

```cas
SAMPLE04 START
         LD      GR1, LEN
         SUBA    GR1, ONE        ; 外側カウンタ = N-1
OUTER    LAD     GR2, 0          ; 内側インデックス = 0
INNER    LD      GR3, ARRAY, GR2 ; ARRAY[i]
         LAD     GR2, 1, GR2     ; i++
         LD      GR4, ARRAY, GR2 ; ARRAY[i+1]
         CPA     GR3, GR4
         JPL     SWAP            ; i > i+1 なら交換
NEXT     CPA     GR2, GR1
         JMI     INNER           ; i < N-1 なら継続
         SUBA    GR1, ONE
         JNZ     OUTER           ; N > 0 なら継続
         RET
SWAP     LAD     GR5, 0, GR2
         SUBA    GR5, ONE        ; GR5 = i
         ST      GR4, ARRAY, GR5 ; ARRAY[i] = 元のARRAY[i+1]
         ST      GR3, ARRAY, GR2 ; ARRAY[i+1] = 元のARRAY[i]
         JUMP    NEXT
ARRAY    DC      64, 34, 25, 12, 22, 11, 90
LEN      DC      7
         END
```

- **交換のテクニック**: 一時レジスタ（GR3/GR4）に両要素を読み、逆順にSTで書き戻します
- **二重ループ**: 外側（OUTER）が「残り要素数」、内側（INNER）が「隣接比較」を担当

**期待値**: `ARRAY` が `[11, 12, 22, 25, 34, 64, 90]` に昇順ソートされる

**学べること**: 多重ループ・隣接交換・指標修飾による読み書き

---

### 05. ナル終端文字列の長さ（`sample05_string_length_count.cas`）

0（ナル文字）で終わる文字列の長さを、末尾まで走査して数えます。

```cas
SAMPLE05 START
         LAD     GR1, 0          ; カウンタ = 0
LOOP     LD      GR2, STR, GR1   ; STR[GR1]
         CPA     GR2, ZERO       ; ナル文字か?
         JZE     DONE
         LAD     GR1, 1, GR1     ; カウンタ++
         JUMP    LOOP
DONE     LD      GR0, GR1
         RET
STR      DC      'CometVM Test'
         DC      0               ; ナル終端
ZERO     DC      0
         END
```

- **`DC 0`**: ナル終端文字（文字列の終わりを示す0）
- **`JZE`**: 直前の演算結果が0なら分岐（ZF=1で分岐）——ナル文字の検出に使用

**期待値**: `GR0 = 12`（'CometVM Test' の長さ）

**学べること**: ナル終端の扱い・指標走査・JZEによる0検出

---

### 06. 小文字→大文字変換（`sample06_case_conversion.cas`）

ASCIIコードの性質を使ったビット演算の応用。小文字は大文字と**ビット5だけが異なる**ことを利用します。

```cas
SAMPLE06 START
         LAD     GR1, 0
         LD      GR2, LEN
LOOP     CPA     GR1, GR2
         JZE     DONE
         LD      GR3, STR, GR1   ; 文字を読み込み
         CPA     GR3, CHAR_A
         JMI     NEXT            ; 'a'未満はスキップ
         CPA     GR3, CHAR_Z
         JPL     NEXT            ; 'z'超はスキップ
         AND     GR3, MASK_UPPER ; ビット5をクリア
         ST      GR3, STR, GR1   ; 大文字で書き戻し
NEXT     LAD     GR1, 1, GR1
         JUMP    LOOP
DONE     OUT     STR, LEN
         RET
STR      DC      'hello world'
LEN      DC      11
CHAR_A   DC      'a'
CHAR_Z   DC      'z'
MASK_UPPER DC    #FFDF           ; ビット5を0にするマスク
         END
```

- **ASCIIの性質**: 小文字（0x61-0x7A）と大文字（0x41-0x5A）は、**ビット5（0x20）だけが違う**。AND #FFDF でビット5を消すと小文字→大文字になる
- **範囲チェック**: 'a'〜'z' の範囲内だけ変換（JMI/JPLで判定）

**期待値**: `HELLO WORLD` が出力される（'hello world' が大文字化）

**学べること**: ビットマスク・AND演算・ASCIIコードの性質・範囲チェック

---

### 07. サブルーチンによる階乗計算（`sample07_factorial_subroutine.cas`）

`CALL`/`RET` によるサブルーチン分割と、`RPUSH`/`RPOP` によるレジスタ保護を学びます。

```cas
SAMPLE07 START
         LD      GR1, N          ; GR1 = 6
         CALL    FACT            ; 階乗計算
         RET
FACT     RPUSH                   ; レジスタ一括退避
         LAD     GR0, 1          ; 積の初期値 = 1
FACT_LOOP CPA    GR1, ONE
         JMI     FACT_DONE       ; N<=1なら終了
         CALL    MULT_GR0_GR1    ; GR0 = GR0 * GR1
         SUBA    GR1, ONE
         JUMP    FACT_LOOP
FACT_DONE RPOP                   ; レジスタ一括復元
         RET
MULT_GR0_GR1 RPUSH
         LD      GR2, GR0
         LAD     GR0, 0
M_LOOP   CPA     GR1, ZERO
         JZE     M_DONE
         ADDA    GR0, GR2        ; 加算の繰り返しで乗算
         SUBA    GR1, ONE
         JUMP    M_LOOP
M_DONE   RPOP
         RET
N        DC      6
ONE      DC      1
ZERO     DC      0
         END
```

- **`CALL`/`RET`**: サブルーチンを呼び出し、終わったら呼び出し元に戻る（戻り先アドレスはスタックに保存）
- **`RPUSH`/`RPOP`**: 全レジスタを一括でスタックに退避・復元——サブルーチン内でレジスタを壊しても呼び出し元に影響しない
- **乗算の実装**: COMET IIには乗算命令がないため、加算の繰り返しで実現

**期待値**: `GR0 = 720`（6! = 6×5×4×3×2×1）

**学べること**: CALL/RET・RPUSH/RPOP・サブルーチン分割・レジスタ保護の作法

---

### 08. 16進文字→数値変換（`sample08_hex_to_decimal.cas`）

ASCII文字の'F'を数値の15に変換します。文字コードの引き算で変換する基本パターンです。

```cas
SAMPLE08 START
         LD      GR1, CHAR_HEX   ; 'F'
         CPA     GR1, CHAR_9
         JZE     IS_DIGIT
         JMI     IS_DIGIT        ; '0'-'9'の場合
         SUBA    GR1, CHAR_A     ; 'A'-'F': 文字 - 'A' + 10
         ADDA    GR1, TEN
         LD      GR0, GR1
         RET
IS_DIGIT SUBA    GR1, CHAR_0     ; '0'-'9': 文字 - '0'
         LD      GR0, GR1
         RET
CHAR_HEX DC      'F'
CHAR_0   DC      '0'
CHAR_9   DC      '9'
CHAR_A   DC      'A'
TEN      DC      10
         END
```

- **数字の変換**: `'7' - '0' = 7`（ASCIIコードの差がそのまま数値）
- **16進文字の変換**: `'F' - 'A' + 10 = 15`
- **文字定数**: `DC 'F'` は文字コード（0x46）が配置される

**期待値**: `GR0 = 15`（'F' を変換）

**学べること**: 文字コード演算・条件分岐による振り分け

---

### 09. キーボード入力＆オウム返し（`sample09_keyboard_input_echo.cas`）

`IN` マクロ命令で入力を受け取り、`OUT` で表示します。

```cas
SAMPLE09 START
         OUT     PROMPT, PROMPT_LEN ; 'Input text: ' を表示
         IN      BUF, BUF_LEN       ; キーボード入力
         OUT     PREFIX, PRE_LEN    ; 'You typed: ' を表示
         OUT     BUF, BUF_LEN       ; 入力文字列を表示
         RET
PROMPT   DC      'Input text: '
PROMPT_LEN DC    12
PREFIX   DC      'You typed: '
PRE_LEN  DC      11
BUF      DS      256              ; 入力バッファ
BUF_LEN  DS      1                ; 入力長を格納する領域
         END
```

- **`IN` マクロ**: `IN 領域, 長さ` の形で、入力文字列を領域に格納し、長さを長さ領域に書き込む
- **`DS` 命令**: 未初期化の領域を確保（バッファ）

**期待値**: 入力した文字列が `You typed: ...` と続けて表示される

**学べること**: INマクロ・DSによるバッファ確保・入出力の組み合わせ

---

### 10. スタック操作デモ（`sample10_stack_push_pop_demo.cas`）

`PUSH`/`POP` の LIFO（後入れ先出し）の動作を確認します。

```cas
SAMPLE10 START
         LD      GR1, VAL10      ; GR1 = 10
         LD      GR2, VAL20      ; GR2 = 20
         LD      GR3, VAL30      ; GR3 = 30
         PUSH    0, GR1          ; 10 を積む（Bottom）
         PUSH    0, GR2          ; 20 を積む
         PUSH    0, GR3          ; 30 を積む（Top）
         LAD     GR1, 0          ; レジスタをクリア
         LAD     GR2, 0
         LAD     GR3, 0
         POP     GR1             ; GR1 = 30（最後に積んだもの）
         POP     GR2             ; GR2 = 20
         POP     GR3             ; GR3 = 10
         RET
VAL10    DC      10
VAL20    DC      20
VAL30    DC      30
         END
```

- **`PUSH`/`POP`**: スタック（メモリ上の後入れ先出し領域）に積む・取り出す
- **LIFOの性質**: 最後に積んだものが最初に出てくる——10, 20, 30 の順で積むと、30, 20, 10 の順で出てくる
- **`PUSH 0, GR1`**: レジスタ番号を即値で指定してPUSHする形式

**期待値**: `GR1 = 30`、`GR2 = 20`、`GR3 = 10`（逆順に復元）

**学べること**: PUSH/POP・LIFOの原理・スタックの用途（後述のサブルーチンでの退避に発展）
