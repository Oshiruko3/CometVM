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
// COMET VM - 命令実行ループの実装
// 設計: docs/comet_vm_design.md・comet.h を参照
//
// 機械語フォーマット（COMETの仕様・試験の定義に準拠）:
//   ワード1: [opcode:8bit][GR:3bit][XR:3bit]（16bit）
//   ワード2: [アドレス:16bit]（アドレスを取る命令のみ）

#include "comet.h"
#include <cstdio>

namespace comet {

// ---- ワードアクセス（ビッグエンディアン） ----
uint16_t Comet::readWord(uint16_t addr) const {
    return mem[addr];
}

void Comet::writeWord(uint16_t addr, uint16_t val) {
    mem[addr] = val;
}

// ---- リセット ----
void reset(Comet& c) {
    for (auto& r : c.GR) r = 0;
    c.PC = OBJ_BASE;
    c.FR = 0;
    for (auto& b : c.mem) b = 0;
}

// ---- フラグ更新（OVF/S/Z） ----
// result は16bitに収まる演算結果。OVFは結果が16bitに収まらなかった場合
void updateFlags(Comet& c, int32_t result) {
    c.FR = 0;
    int16_t r16 = static_cast<int16_t>(result & 0xFFFF);
    if (r16 < 0) c.FR |= FR_S;
    if (r16 == 0) c.FR |= FR_Z;
    // オーバーフロー: 符号付き16bitの範囲を超えた場合
    if (result < -32768 || result > 32767) c.FR |= FR_OVF;
}

// ---- 実効アドレス計算（adr + XRの内容） ----
static uint16_t effAddr(const Comet& c, uint16_t adr, uint8_t xr) {
    if (xr > 0) {
        return static_cast<uint16_t>(adr + c.GR[xr]);
    }
    return adr;
}

// ---- 1命令実行 ----
void step(Comet& c, bool /*trace*/) {
    // 命令フェッチ（ワード1: opcode+GR+XR）
    uint16_t opw = c.readWord(c.PC);
    c.PC += 1;

    uint8_t op   = static_cast<uint8_t>(opw >> 8);
    uint8_t gr   = static_cast<uint8_t>((opw >> 3) & 0x07);
    uint8_t xr   = static_cast<uint8_t>(opw & 0x07);
    if (gr >= NUM_GR) { gr = 0; }  // 不正なGRはGR0に（防御）

    // アドレスを取る命令は次のワードをフェッチ
    bool hasAddr = (op == OP_LD || op == OP_LAD || op == OP_ST || op == OP_LEA ||
                    op == OP_ADDA || op == OP_ADDL || op == OP_SUBA || op == OP_SUBL ||
                    op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_CPA ||
                    op == OP_JMP || op == OP_JPZ || op == OP_JNZ || op == OP_JMI ||
                    op == OP_JPL || op == OP_JOV || op == OP_CALL);
    // IN/OUTは2つのアドレスを持つ（出力領域・文字長領域）
    bool hasAddr2 = (op == OP_IN || op == OP_OUT);
    uint16_t adr = 0, adr2 = 0;
    if (hasAddr) {
        adr = c.readWord(c.PC);
        c.PC += 1;
    }
    if (hasAddr2) {
        adr = c.readWord(c.PC);
        adr2 = c.readWord(c.PC + 1);
        c.PC += 2;
    }

    switch (op) {
        // ---- ロード・ストア系 ----
        case OP_LD:
            c.GR[gr] = static_cast<int16_t>(c.readWord(effAddr(c, adr, xr)));
            break;
        case OP_LAD:
            // LAD: GR ← adr + (XR) —— ループカウンタの定番（LAD GR1,1,GR1 = インクリメント）
            c.GR[gr] = static_cast<int16_t>(effAddr(c, adr, xr));
            break;
        case OP_LEA: {
            // LEA: GR ← GR + (adr + XRの内容) —— ループカウンタ・アドレス計算
            int32_t r = static_cast<int32_t>(c.GR[gr]) + effAddr(c, adr, xr);
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, r);
            break;
        }
        case OP_ST:
            c.writeWord(effAddr(c, adr, xr), static_cast<uint16_t>(c.GR[gr]));
            break;

        // ---- 算術演算系 ----
        case OP_ADDA: {
            int32_t r = static_cast<int32_t>(c.GR[gr]) +
                        static_cast<int32_t>(c.readWord(effAddr(c, adr, xr)));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, r);
            break;
        }
        case OP_ADDL: {
            uint32_t r = static_cast<uint32_t>(c.GR[gr]) +
                         static_cast<uint32_t>(c.readWord(effAddr(c, adr, xr)));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, static_cast<int32_t>(r & 0xFFFF));
            break;
        }
        case OP_SUBA: {
            int32_t r = static_cast<int32_t>(c.GR[gr]) -
                        static_cast<int32_t>(c.readWord(effAddr(c, adr, xr)));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, r);
            break;
        }
        case OP_SUBL: {
            uint32_t r = static_cast<uint32_t>(c.GR[gr]) -
                         static_cast<uint32_t>(c.readWord(effAddr(c, adr, xr)));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, static_cast<int32_t>(r & 0xFFFF));
            break;
        }

        // ---- 論理演算系 ----
        case OP_AND:
            c.GR[gr] &= static_cast<int16_t>(c.readWord(effAddr(c, adr, xr)));
            updateFlags(c, c.GR[gr]);
            break;
        case OP_OR:
            c.GR[gr] |= static_cast<int16_t>(c.readWord(effAddr(c, adr, xr)));
            updateFlags(c, c.GR[gr]);
            break;
        case OP_XOR:
            c.GR[gr] ^= static_cast<int16_t>(c.readWord(effAddr(c, adr, xr)));
            updateFlags(c, c.GR[gr]);
            break;

        // ---- 比較（フラグのみ更新） ----
        case OP_CPA: {
            int32_t a = c.GR[gr];
            int32_t b = static_cast<int16_t>(c.readWord(effAddr(c, adr, xr)));
            updateFlags(c, a - b);
            break;
        }

        // ---- シフト ----
        case OP_SLL:
            c.GR[gr] = static_cast<int16_t>(static_cast<uint16_t>(c.GR[gr]) << adr);
            updateFlags(c, c.GR[gr]);
            break;
        case OP_SRL:
            c.GR[gr] = static_cast<int16_t>(static_cast<uint16_t>(c.GR[gr]) >> adr);
            updateFlags(c, c.GR[gr]);
            break;

        // ---- 分岐 ----
        case OP_JMP: c.PC = adr; break;
        case OP_JPZ: if (c.FR & FR_Z) c.PC = adr; break;
        case OP_JNZ: if (!(c.FR & FR_Z)) c.PC = adr; break;
        case OP_JMI: if (c.FR & FR_S) c.PC = adr; break;
        case OP_JPL: if (!(c.FR & FR_S)) c.PC = adr; break;
        case OP_JOV: if (c.FR & FR_OVF) c.PC = adr; break;

        // ---- スタック・サブルーチン ----
        case OP_CALL: {
            c.GR[SP_REG] -= 2;
            c.writeWord(static_cast<uint16_t>(c.GR[SP_REG]), c.PC);
            c.PC = adr;
            break;
        }
        case OP_RET: {
            c.PC = c.readWord(static_cast<uint16_t>(c.GR[SP_REG]));
            c.GR[SP_REG] += 2;
            break;
        }
        case OP_PUSH:
            c.GR[SP_REG] -= 2;
            c.writeWord(static_cast<uint16_t>(c.GR[SP_REG]), static_cast<uint16_t>(c.GR[gr]));
            break;
        case OP_POP:
            c.GR[gr] = static_cast<int16_t>(c.readWord(static_cast<uint16_t>(c.GR[SP_REG])));
            c.GR[SP_REG] += 2;
            break;

        // ---- 入出力（トレース時は簡易表示） ----
        case OP_IN: {
            // 入力: コールバック（in_func）から文字列を取得して指定領域に格納
            // COMET IIの文字は1語の下位8bitに格納（DC 'A' = 0x0041）
            uint16_t base = effAddr(c, adr, xr);
            uint16_t lenAdr = effAddr(c, adr2, 0);
            if (c.in_func) {
                char buf[128];
                int n = c.in_func(buf, sizeof(buf));
                if (n > 64) n = 64;  // 防御（最大64文字）
                for (int i = 0; i < n; i++) {
                    c.writeWord(base + i, static_cast<uint8_t>(buf[i]));
                }
                c.writeWord(lenAdr, static_cast<uint16_t>(n));
            } else {
                // コールバック未設定は従来のダミー動作（0を読む）
                c.writeWord(base, 0);
                c.writeWord(lenAdr, 0);
            }
            break;
        }
        case OP_OUT: {
            // 出力: adr（出力領域）から、adr2（文字長領域）の値ぶんの文字を出力
            // COMET IIの文字は1語の下位8bitに格納（DC 'A' = 0x0041）
            uint16_t base = effAddr(c, adr, xr);
            uint16_t len = c.readWord(effAddr(c, adr2, 0));
            if (len > 64) len = 64;  // 防御（最大64文字）
            for (uint16_t i = 0; i < len; i++) {
                uint16_t w = c.mem[base + i];
                if (w == 0) break;
                char ch = static_cast<char>(w & 0xFF);
                // 出力先はコールバック（TFT等）・未設定ならシリアル
                if (c.out_func) {
                    c.out_func(ch);
                } else {
                    std::putchar(ch);
                }
            }
            break;
        }

        case OP_NOP:
        default:
            break;
    }
}

} // namespace comet
