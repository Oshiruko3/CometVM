# COMET VM

CASL II 仮想マシン（COMET II エミュレータ + CASL2 アセンブラ）

情報処理技術者試験の CASL II / COMET II を忠実に再現する16bit仮想マシンと、CASL2アセンブラです。PCでも、ESP32（M5Cardputer）でも動作します。

## 特徴

- **正式なCOMET II準拠**——機械語形式（8-4-4）とオペコード表を、情報処理技術者試験のCOMET II仕様に合わせて実装
- **フラグ厳密**——OF/SF/ZFの更新タイミングを忠実に再現（CASL2の分岐判定に必須）
- **学習用サンプル10本同梱**——Hello Worldからスタック操作まで、コメント丁寧・期待値つき（`samples/README.md`）
- **Cardputer対応**——M5Cardputerで実機動作（`firmware/`）

## ディレクトリ構成

```
comet-vm/
├── vm/              # PCで動くコア
│   ├── Makefile     # make / make sample
│   ├── main.cpp     # コマンドライン実行ツール
│   └── src/         # comet.h/cpp（VM本体）・casl_asm.h/cpp（アセンブラ）
├── firmware/        # Cardputer用（ESP32・PlatformIO）
│   ├── platformio.ini
│   ├── src/main.cpp # 画面表示・キーボード・COMET VM実行
│   └── lib/         # comet（コア共有）・g850_ui（TFT/キーボードドライバ）
└── samples/         # 学習用サンプル10本 + 解説README
```

## ビルドと実行（PC）

```bash
cd vm
make                  # cometvm をビルド
./cometvm ../samples/sample01_hello_world_out.cas   # サンプルを実行
make sample           # 全サンプルを実行
```

## Cardputer（ESP32）

`firmware/` は M5Cardputer（M5Stack Cardputer ADV）向けのファームウェアです。
PlatformIO でビルドし、SDカード経由で書き込みます（M5Launcher からの起動に対応）。

- ハードウェア: M5Stack Cardputer ADV（ESP32-S3・240x135 TFT・TCA8418キーボード）
- 依存: LovyanGFX（MIT License）

## 学習用サンプル

`samples/` に、Hello Worldからスタック操作まで段階的に学べる10本のサンプルがあります。
各サンプルに期待値とコードの解説がついているので、プログラムを実行して期待値と照合しながら学習できます。

## 参考資料

- CASL II 仕様（情報処理技術者試験・IPA）
- SHARP PC-G850V（ポケコン・本プロジェクトの参考モデル）
- [h-ohsaki/casl](https://github.com/h-ohsaki/casl)（調査元として参照）

## ライセンス

**GPL-3.0**（個人・教育・学習用途は自由に利用・改変・再配布できます）

商用利用（製品への組み込み・販売等）を検討される場合は、商用ライセンスの取得が必要です。
ご相談はリポジトリのIssueまたはメンテナへお問い合わせください。
