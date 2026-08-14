// COMET VM - CASL II 仮想マシン（COMET II エミュレータ + CASL2 アセンブラ）
// Copyright (C) 2026 K. Matsumoto
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// COMET VM - COMET II仮想マシン（CASL2用）
// ポケコン復活プロジェクト フェーズ1-第2段階 ①
// 設計: docs/comet_vm_design.md を参照
//
// COMET II: 基本情報技術者試験（旧）のCASL2用16bit仮想マシン
// - 1語16bit・アドレス空間0x0000〜0xFFFF
// - レジスタ: GR0〜GR7（GR4=SP）・PC・FR（OVF/S/Zフラグ）
// - オブジェクトは#1000番地から格納（G850Vの仕様）
// ※決定事項#3: 参考モデルG850VのCASLがCASL2であるため、COMET II仕様で実装

#ifndef COMET_H
#define COMET_H

#include <cstdint>
#include <cstddef>

namespace comet {

// レジスタ数（COMET II仕様: GR0〜GR7・GR4=スタックポインタ）
constexpr int NUM_GR = 8;
// 8/13修正: COMET II の SP は GR と独立した制御レジスタ（従来「GR4=SP」と誤っていた）
// 汎用レジスタ GR0〜GR7 の8本 + 制御レジスタ SP（スタックポインタ）・PC・FR
constexpr size_t COMET_MEM_SIZE = 0x10000;  // 65536語（COMET IIは語アドレス空間）※8/13修正: uint16_tだと0x10000が0にラップしサイズ0配列になっていた
constexpr uint16_t OBJ_BASE = 0x1000;   // オブジェクト格納開始アドレス（G850Vの仕様・語アドレス）

// フラグレジスタ（FR）のビット定義
constexpr uint16_t FR_OVF = 0x01;  // オーバーフローフラグ
constexpr uint16_t FR_S   = 0x02;  // サインフラグ（負）
constexpr uint16_t FR_Z   = 0x04;  // ゼロフラグ

// 命令コード（機械語命令・COMET II仕様）
// 8/14修正: オペコードを正式なCOMET II（情報処理技術者試験）に合わせた
//   （従来は独自の表だった——共同開発時に確認したtest14の#1070(LD)が食い違いを暴いた）
enum Opcode : uint8_t {
    // ロード・ストア系
    OP_LEA = 0x01,   // LEA gr, adr[,xr]  - GRにadr+xrの内容を加算（ループカウンタの定番）
    OP_LAD = 0x02,   // LAD gr, adr[,xr]  - ロードアドレス
    OP_ST  = 0x03,   // ST  gr, adr[,xr]  - ストア
    OP_LD  = 0x10,   // LD  gr, adr[,xr]  - ロード
    // 算術演算系
    OP_ADDA = 0x20,  // ADDA gr, adr[,xr] - 算術加算
    OP_ADDL = 0x21,  // ADDL gr, adr[,xr] - 論理加算
    OP_SUBA = 0x22,  // SUBA gr, adr[,xr] - 算術減算
    OP_SUBL = 0x23,  // SUBL gr, adr[,xr] - 論理減算
    // 論理演算系
    OP_AND = 0x24,   // AND gr, adr[,xr]  - 論理積
    OP_OR  = 0x25,   // OR  gr, adr[,xr]  - 論理和
    OP_XOR = 0x26,   // XOR gr, adr[,xr]  - 排他的論理和
    // 比較・シフト系
    OP_CPA = 0x30,   // CPA gr, adr[,xr]  - 算術比較
    OP_CPL = 0x31,   // CPL gr, adr[,xr]  - 論理比較（符号なし）
    OP_SLL = 0x40,   // SLL gr, n         - 論理左シフト
    OP_SRA = 0x41,   // SRA gr, n         - 算術右シフト（符号ビットで埋める・8/13追加: c10.cas）
    OP_SRL = 0x42,   // SRL gr, n         - 論理右シフト
    OP_SLA = 0x43,   // SLA gr, n         - 算術左シフト（ビット15を保持・拡張命令: h-ohsaki sla.cas）
    // 分岐・制御系
    OP_JMP = 0x50,   // JMP adr           - 無条件分岐
    OP_JPZ = 0x51,   // JPZ adr           - FR.S=0で分岐（非負・8/13修正: 従来Z=1誤り）
    OP_JNZ = 0x52,   // JNZ adr           - FR.Z=0で分岐
    OP_JMI = 0x53,   // JMI adr           - FR.S=1で分岐
    OP_JPL = 0x54,   // JPL adr           - FR.S=0かつFR.Z=0で分岐（正・8/13修正: 従来S=0のみ誤り）
    OP_JOV = 0x55,   // JOV adr           - FR.OVF=1で分岐
    OP_JZE = 0x56,   // JZE adr           - FR.Z=1で分岐（8/13追加: 従来JPZの別名だった誤りを独立命令化）
    OP_CALL = 0x60,  // CALL adr          - サブルーチン呼び出し
    OP_RET = 0x61,   // RET               - リターン
    OP_PUSH = 0x62,  // PUSH gr           - スタックに格納
    OP_POP = 0x63,   // POP gr            - スタックから復元
    // 入出力・その他
    OP_IN  = 0x70,   // IN  adr, len      - 入力
    OP_OUT = 0x71,   // OUT adr, len      - 出力
    OP_NOP = 0x7F,   // NOP               - 無操作
};

// COMETの状態
struct Comet {
    int16_t GR[NUM_GR] = {};    // 汎用レジスタ（GR0〜GR7）
    uint16_t SP = 0;            // スタックポインタ（制御レジスタ・GRと独立・8/13修正）
    uint16_t PC = OBJ_BASE;     // プログラムカウンタ（語アドレス）
    uint16_t FR = 0;            // フラグレジスタ
    uint16_t mem[COMET_MEM_SIZE] = {}; // メモリ（65536語・語単位のアドレス空間）

    // 出力コールバック（OUT命令の出力先・nullptrならputchar）
    void (*out_func)(char) = nullptr;

    // 入力コールバック（IN命令の入力元・nullptrならダミー動作）
    // bufに文字列を取得して長さを返す（0=入力なし）
    int (*in_func)(char* buf, int maxLen) = nullptr;

    // ワードアクセス（1語=16bit）
    uint16_t readWord(uint16_t addr) const;
    void writeWord(uint16_t addr, uint16_t val);
};

// 1命令実行（トレースモード対応: trueで1ステップ停止）
void step(Comet& c, bool trace = false);

// リセット（メモリクリア・レジスタ初期化）
void reset(Comet& c);

// フラグ更新（演算結果からOVF/S/Zを設定）
void updateFlags(Comet& c, int32_t result);

} // namespace comet

#endif // COMET_H
