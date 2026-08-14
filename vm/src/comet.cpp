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
//   ワード1: [opcode:8bit][GR:4bit][XR:4bit]（16bit）
//      ※8/14修正: GRはbit7-4・XRはbit3-0（従来の8-3-3形式は独自仕様だった——
//       共同開発時に確認したtest14の#1070（LD GR7,ADDR = 0x1070）が8-4-4形式で書かれており食い違いを暴いた）
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
    c.SP = 0;               // スタックポインタ（GRと独立・8/13修正）
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

// オペランド値の取得（8/13追加）
// CASL2のレジスタ間指定（op gr1,gr2）は、アセンブラが adr = 0xFF00 | GR番号 で
// エンコードする——VMはこれを「レジスタの内容」として解釈する
static int16_t getOperand(const Comet& c, uint16_t adr, uint8_t xr) {
    if (adr >= 0xFF00) {
        return c.GR[adr & 0x07];  // レジスタ間演算
    }
    return static_cast<int16_t>(c.readWord(effAddr(c, adr, xr)));
}

// ---- 1命令実行 ----
void step(Comet& c, bool /*trace*/) {
    // 命令フェッチ（ワード1: opcode+GR+XR）
    uint16_t opw = c.readWord(c.PC);
    c.PC += 1;

    uint8_t op   = static_cast<uint8_t>(opw >> 8);
    // 8/14修正: GRはbit7-4・XRはbit3-0（8-4-4形式——test14の#1070 = LD GR7,ADDR に合わせた）
    uint8_t gr   = static_cast<uint8_t>((opw >> 4) & 0x07);
    uint8_t xr   = static_cast<uint8_t>(opw & 0x07);
    if (gr >= NUM_GR) { gr = 0; }  // 不正なGRはGR0に（防御）

    // アドレスを取る命令は次のワードをフェッチ
    bool hasAddr = (op == OP_LD || op == OP_LAD || op == OP_ST || op == OP_LEA ||
                    op == OP_ADDA || op == OP_ADDL || op == OP_SUBA || op == OP_SUBL ||
                    op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_CPA ||
                    op == OP_CPL ||  // 論理比較（8/13修正: 追加し忘れていた）
                    op == OP_SLL || op == OP_SRL || op == OP_SRA || op == OP_SLA ||  // gr,定数——シフト量をadrで読む（8/13修正: 欠落していた・SRA/SLA追加）
                    op == OP_JMP || op == OP_JPZ || op == OP_JNZ || op == OP_JMI ||
                    op == OP_JPL || op == OP_JOV || op == OP_JZE || op == OP_CALL);
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
            // 8/13修正: CASL2ではLDはFRを更新する（転送値でZF/SF・OFは0）——b06.casで発見
            // 従来「LDはFRを変更しない」と誤っていた（JZE直後の分岐が効かなかった）
            c.GR[gr] = getOperand(c, adr, xr);
            updateFlags(c, c.GR[gr]);
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
                        static_cast<int32_t>(getOperand(c, adr, xr));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, r);
            break;
        }
        case OP_ADDL: {
            // 8/13修正: 論理演算は符号なし（uint16_t）で扱う——int16_tの-1(0xFFFF)を
            // uint32_tにキャストすると0xFFFFFFFFになり加算がラップする（h-ohsaki inc.cas）
            uint32_t r = static_cast<uint16_t>(c.GR[gr]) +
                         static_cast<uint16_t>(getOperand(c, adr, xr));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            // 8/13修正: 16ビット結果をint16_tとして評価（int32_tのまま渡すと
            //   0x8000超で符号付きOVFが誤検出される——h-ohsaki addl.cas）
            updateFlags(c, static_cast<int16_t>(r & 0xFFFF));
            // 8/13修正: 符号なしOVF（0xFFFF超）——h-ohsaki inc.casで発見（従来検出漏れ）
            if (r > 0xFFFF) c.FR |= FR_OVF;
            break;
        }
        case OP_SUBA: {
            int32_t r = static_cast<int32_t>(c.GR[gr]) -
                        static_cast<int32_t>(getOperand(c, adr, xr));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            updateFlags(c, r);
            break;
        }
        case OP_SUBL: {
            // 8/13修正: 論理演算は符号なし（uint16_t）で扱う（h-ohsaki sub.cas）
            uint32_t r = static_cast<uint16_t>(c.GR[gr]) -
                         static_cast<uint16_t>(getOperand(c, adr, xr));
            c.GR[gr] = static_cast<int16_t>(r & 0xFFFF);
            // 8/13修正: 16ビット結果をint16_tとして評価（h-ohsaki addl.cas）
            updateFlags(c, static_cast<int16_t>(r & 0xFFFF));
            // 8/13修正: 符号なしOVF（借りが発生）——h-ohsaki sub.casで発見（従来検出漏れ）
            if (r > 0xFFFF) c.FR |= FR_OVF;
            break;
        }

        // ---- 論理演算系 ----
        case OP_AND:
            c.GR[gr] &= getOperand(c, adr, xr);
            updateFlags(c, c.GR[gr]);
            break;
        case OP_OR:
            c.GR[gr] |= getOperand(c, adr, xr);
            updateFlags(c, c.GR[gr]);
            break;
        case OP_XOR:
            c.GR[gr] ^= getOperand(c, adr, xr);
            updateFlags(c, c.GR[gr]);
            break;

        // ---- 比較（フラグのみ更新） ----
        case OP_CPA: {
            // 算術比較（符号付き）——8/14修正: 減算のオーバーフローを無視して比較する
            // （従来は a-b を評価し、#8000 vs #7FFF で -65535 が16bitに丸まって誤判定——
            //   共同開発時に確認した test16: CPA #8000,#7FFF → SF=1 が期待）
            int16_t a = c.GR[gr];
            int16_t b = static_cast<int16_t>(getOperand(c, adr, xr));
            c.FR = 0;
            if (a < b) c.FR |= FR_S;   // 符号付きで a < b
            if (a == b) c.FR |= FR_Z;  // 等しい
            break;
        }
        case OP_CPL: {
            // 論理比較（符号なし）: GR と メモリ[adr+xr] を16bit符号なしで比較
            // 8/13修正: フラグは符号なし比較で立てる（従来は a-b を符号付きで評価し
            //   a<b のとき負の大きな値になり OVF が立っていた——h-ohsaki cpl.cas）
            uint16_t a = static_cast<uint16_t>(c.GR[gr]);
            uint16_t b = static_cast<uint16_t>(getOperand(c, adr, xr));
            c.FR = 0;
            if (a < b) c.FR |= FR_S;   // 符号なしで a < b
            if (a == b) c.FR |= FR_Z;  // 等しい
            break;
        }

        // ---- シフト ----
        case OP_SLL: {
            // 左シフト——シフト量は実行アドレスの値（adr + GR[xr]・8/13修正: 従来xrを無視していた）
            // シフトアウトしたビット（上位shiftビット）が1ならOVF（8/13修正: t02.casで発見）
            uint16_t shift = effAddr(c, adr, xr);
            if (shift > 16) shift = 16;  // 防御
            uint16_t v = static_cast<uint16_t>(c.GR[gr]);
            uint16_t shifted = static_cast<uint16_t>(v << shift);
            c.GR[gr] = static_cast<int16_t>(shifted);
            updateFlags(c, c.GR[gr]);
            if (shift > 0) {
                uint16_t overflow = static_cast<uint16_t>(v >> (16 - shift)) &
                                    static_cast<uint16_t>((1u << shift) - 1u);
                if (overflow != 0) c.FR |= FR_OVF;
            }
            break;
        }
        case OP_SRL: {
            // 右シフト——シフト量は実行アドレスの値（adr + GR[xr]・8/13修正: 従来xrを無視していた）
            // シフトアウトしたビット（下位shiftビット）が1ならOVF
            uint16_t shift = effAddr(c, adr, xr);
            if (shift > 16) shift = 16;  // 防御
            uint16_t v = static_cast<uint16_t>(c.GR[gr]);
            uint16_t shifted = static_cast<uint16_t>(v >> shift);
            c.GR[gr] = static_cast<int16_t>(shifted);
            updateFlags(c, c.GR[gr]);
            if (shift > 0) {
                uint16_t overflow = static_cast<uint16_t>(v & ((1u << shift) - 1u));
                if (overflow != 0) c.FR |= FR_OVF;
            }
            break;
        }
        case OP_SRA: {
            // 算術右シフト——空いた上位ビットを符号ビットで埋める（8/13追加: c10.cas）
            uint16_t shift = effAddr(c, adr, xr);
            if (shift > 16) shift = 16;  // 防御
            uint16_t v = static_cast<uint16_t>(c.GR[gr]);
            uint16_t sign = (v & 0x8000) ? 0xFFFF : 0x0000;
            uint16_t shifted = static_cast<uint16_t>((v >> shift) | (sign << (16 - shift)));
            c.GR[gr] = static_cast<int16_t>(shifted);
            updateFlags(c, c.GR[gr]);
            if (shift > 0) {
                uint16_t overflow = static_cast<uint16_t>(v & ((1u << shift) - 1u));
                if (overflow != 0) c.FR |= FR_OVF;
            }
            break;
        }
        case OP_SLA: {
            // 算術左シフト——ビット15（符号）を保持し、下位15ビットを左シフト（8/13修正: h-ohsaki sla.cas）
            uint16_t shift = effAddr(c, adr, xr);
            if (shift > 15) shift = 15;  // 防御（15ビット超は下位15ビット全部がシフトアウト）
            uint16_t v = static_cast<uint16_t>(c.GR[gr]);
            uint16_t sign = v & 0x8000;
            uint16_t shifted = static_cast<uint16_t>(((v & 0x7FFF) << shift) | (sign ? 0x8000 : 0));
            c.GR[gr] = static_cast<int16_t>(shifted);
            updateFlags(c, c.GR[gr]);
            uint16_t overflow = static_cast<uint16_t>(((v & 0x7FFF) >> (15 - shift)) &
                                                      ((1u << shift) - 1u));
            if (overflow != 0) c.FR |= FR_OVF;
            break;
        }

        // ---- 分岐（8/13修正: 実行アドレス = adr + GR[xr]——b08.casのJUMP 0,GR5計算ジャンプ対応） ----
        // ※従来は adr そのままにジャンプしていた（xrを無視）——分岐も実行アドレス自体にジャンプする
        case OP_JMP: c.PC = effAddr(c, adr, xr); break;
        // JPZ: 非負（S=0）・JPL: 正（S=0かつZ=0）——8/13修正（従来 JPZがZ=1・JPLがS=0のみと誤っていた）
        case OP_JPZ: if (!(c.FR & FR_S)) c.PC = effAddr(c, adr, xr); break;
        case OP_JNZ: if (!(c.FR & FR_Z)) c.PC = effAddr(c, adr, xr); break;
        case OP_JMI: if (c.FR & FR_S) c.PC = effAddr(c, adr, xr); break;
        case OP_JPL: if (!(c.FR & FR_S) && !(c.FR & FR_Z)) c.PC = effAddr(c, adr, xr); break;
        case OP_JOV: if (c.FR & FR_OVF) c.PC = effAddr(c, adr, xr); break;
        case OP_JZE: if (c.FR & FR_Z) c.PC = effAddr(c, adr, xr); break;

        // ---- スタック・サブルーチン（SPはGRと独立した制御レジスタ・8/13修正） ----
        case OP_CALL: {
            c.SP -= 2;
            c.writeWord(c.SP, c.PC);
            c.PC = effAddr(c, adr, xr);  // 8/13修正: 実行アドレス = adr + GR[xr]
            break;
        }
        case OP_RET: {
            c.PC = c.readWord(c.SP);
            c.SP += 2;
            break;
        }
        case OP_PUSH:
            c.SP -= 2;
            c.writeWord(c.SP, static_cast<uint16_t>(c.GR[gr]));
            break;
        case OP_POP:
            c.GR[gr] = static_cast<int16_t>(c.readWord(c.SP));
            c.SP += 2;
            break;

        // ---- 入出力（トレース時は簡易表示） ----
        case OP_IN: {
            // 入力: コールバック（in_func）から文字列を取得して指定領域に格納
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
