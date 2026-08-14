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
// CASLアセンブラ - 実装（2パス方式）
// 第1パス: ラベルのアドレスを解決（各命令のサイズを計算しながら）
// 第2パス: 機械語を生成（ラベル参照をアドレスで置換）
//
// 機械語フォーマット（comet.hと整合）:
//   ワード1: [opcode:8bit][GR:3bit][XR:3bit]
//   ワード2: [アドレス:16bit]（アドレスを取る命令のみ）
// 文字列DCは2文字ずつ1ワードにパック（'A'<<8|'B'）——OUTのバイト連続出力と整合

#include "casl_asm.h"
#include "comet.h"
#include <cctype>
#include <sstream>
#include <map>

namespace casl {

using namespace comet;

// ---- 命令情報 ----
struct InstInfo {
    const char* name;
    uint8_t opcode;
    int type;   // 0:オペランドなし 1:gr 2:gr,adr[,xr] 3:adr[,xr] 4:gr,定数 5:adr,len(IN/OUT)
};

// ※静的配列で保持（std::mapは起動時の動的初期化が必要で、ESP32でクラッシュするため）
static const InstInfo kInstList[] = {
    {"LD",   OP_LD,   2}, {"LAD", OP_LAD, 2}, {"ST", OP_ST, 2}, {"LEA", OP_LEA, 2},
    {"ADDA", OP_ADDA, 2}, {"ADDL", OP_ADDL, 2},
    {"SUBA", OP_SUBA, 2}, {"SUBL", OP_SUBL, 2},
    {"AND",  OP_AND,  2}, {"OR",  OP_OR,  2}, {"XOR", OP_XOR, 2},
    {"CPA",  OP_CPA,  2},
    {"SLL",  OP_SLL,  4}, {"SRL", OP_SRL, 4},
    {"JMP",  OP_JMP,  3}, {"JPZ", OP_JPZ, 3}, {"JNZ", OP_JNZ, 3},
    {"JMI",  OP_JMI,  3}, {"JPL", OP_JPL, 3}, {"JOV", OP_JOV, 3},
    {"CALL", OP_CALL, 3},
    {"RET",  OP_RET,  0}, {"NOP", OP_NOP, 0},
    {"PUSH", OP_PUSH, 1}, {"POP", OP_POP, 1},
    {"IN",   OP_IN,   5}, {"OUT", OP_OUT, 5},
};

// 命令名から命令情報を探す（線形探索）
static const InstInfo* findInst(const std::string& name) {
    for (const auto& inst : kInstList) {
        if (name == inst.name) return &inst;
    }
    return nullptr;
}

// アセンブラ命令（機械語を生成しない）
static bool isAssemblerInst(const std::string& inst) {
    return inst == "START" || inst == "END" || inst == "DC" || inst == "DS";
}

// マクロ命令（複数命令に展開）
static bool isMacroInst(const std::string& inst) {
    return inst == "RPUSH" || inst == "RPOP";
}

// ---- 文字列ユーティリティ ----
static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

static std::string upper(const std::string& s) {
    std::string r = s;
    for (auto& ch : r) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return r;
}

// ---- 行の解析（ラベル・命令・オペランド） ----
struct Line {
    std::string label;
    std::string inst;
    std::string operand;
    int lineNo;
};

static bool parseLine(const std::string& raw, Line& line) {
    // コメント除去
    std::string s = raw;
    size_t semi = s.find(';');
    if (semi != std::string::npos) s = s.substr(0, semi);
    s = trim(s);
    if (s.empty()) return false;  // 空行

    // 空白で分割（先頭のトークンがラベルか命令かを判定）
    std::istringstream iss(s);
    std::string t1, t2, rest;
    iss >> t1;
    std::string remaining;
    std::getline(iss, remaining);

    // ラベルの判定: 「:」で終わるか、最初のトークンが命令コードでない場合
    auto isInst = [](const std::string& t) {
        std::string u = upper(t);
        return findInst(u) != nullptr || isAssemblerInst(u) || isMacroInst(u);
    };
    if (!t1.empty() && t1.back() == ':') {
        // 「:」付きラベル（例: "X:"）
        line.label = t1.substr(0, t1.size() - 1);
        remaining = trim(remaining);
        if (remaining.empty()) return false;  // ラベルのみの行
        std::istringstream iss2(remaining);
        iss2 >> line.inst;
        std::getline(iss2, rest);
    } else if (isInst(t1)) {
        // 命令コード
        line.inst = t1;
        rest = remaining;
    } else {
        // 「:」なしのラベル（例: "X    DC 5"）
        line.label = t1;
        remaining = trim(remaining);
        if (remaining.empty()) return false;  // ラベルのみの行
        std::istringstream iss2(remaining);
        iss2 >> line.inst;
        std::getline(iss2, rest);
    }

    line.inst = upper(line.inst);
    line.operand = trim(rest);
    return true;
}

// ---- オペランドの解析 ----
// GR番号の解析（"GR1"〜"GR4"）
static bool parseGR(const std::string& s, int& gr) {
    std::string u = upper(s);
    if (u.size() >= 3 && u[0] == 'G' && u[1] == 'R') {
        char c = u[2];
        if (c >= '0' && c <= '7') {
            gr = c - '0';
            return true;
        }
    }
    return false;
}

// 数値定数の解析（10進: -32768〜65535・16進: #0000〜#FFFF）
// ※例外を使わない実装（ESP32のArduino環境は-fno-exceptionsのため）
static bool parseInt(const std::string& s, int& val) {
    // 16進（#FF00形式）
    if (!s.empty() && s[0] == '#') {
        long v = 0;
        for (size_t i = 1; i < s.size(); i++) {
            char ch = s[i];
            int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else return false;
            v = v * 16 + d;
            if (v > 0xFFFF) return false;
        }
        val = static_cast<int>(v);
        return true;
    }
    // 10進
    bool neg = false;
    size_t i = 0;
    if (!s.empty() && s[0] == '-') { neg = true; i = 1; }
    long v = 0;
    for (; i < s.size(); i++) {
        char ch = s[i];
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
        if (v > 65536) return false;
    }
    if (neg) v = -v;
    if (v < -32768 || v > 65535) return false;
    val = static_cast<int>(v);
    return true;
}

// 文字列定数の解析（'ABC' 形式）
static bool parseCharConst(const std::string& s, std::string& out) {
    if (s.size() < 2 || s.front() != '\'' || s.back() != '\'') return false;
    out = s.substr(1, s.size() - 2);
    return true;
}

// ---- アセンブラ本体 ----
AsmResult assemble(const std::string& source) {
    AsmResult result;
    std::vector<Line> lines;
    std::map<std::string, int> labels;  // ラベル → オフセット（#1000からの相対）

    // ---- 行の分割・解析 ----
    std::istringstream iss(source);
    std::string raw;
    int lineNo = 0;
    while (std::getline(iss, raw)) {
        lineNo++;
        Line line;
        line.lineNo = lineNo;
        if (parseLine(raw, line)) {
            lines.push_back(line);
        }
    }

    // ---- 第1パス: 各命令のサイズ計算とラベル解決 ----
    int offset = 0;
    for (auto& line : lines) {
        if (!line.label.empty()) {
            if (labels.count(line.label)) {
                result.error = "LABEL ERROR: ラベル '" + line.label + "' が重複（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            labels[line.label] = offset;
        }
        const std::string& inst = line.inst;
        if (isAssemblerInst(inst)) {
            if (inst == "DC") {
                // 定数の数だけワード（文字列は2文字/ワード）
                std::string cs;
                if (parseCharConst(line.operand, cs)) {
                    offset += static_cast<int>(cs.size());  // 1文字1ワード（COMET II仕様）
                } else {
                    // カンマ区切りの定数
                    int count = 1;
                    for (char ch : line.operand) if (ch == ',') count++;
                    offset += count;
                }
            } else if (inst == "DS") {
                int n = 0;
                if (!parseInt(line.operand, n) || n < 0) {
                    result.error = "OPERAND ERROR: DSの語数が不正（行" +
                                   std::to_string(line.lineNo) + "）";
                    return result;
                }
                offset += n;
            }
            // START/ENDはサイズなし
        } else if (isMacroInst(inst)) {
            offset += 5;  // RPUSH/RPOPは5命令に展開
        } else {
            auto it = findInst(inst);
            if (it == nullptr) {
                result.error = "OP CODE ERROR: 不明な命令 '" + inst + "'（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            int type = it->type;
            // アドレスを取る命令は2ワード
            if (type == 2 || type == 3 || type == 4) offset += 2;
            else if (type == 5) offset += 3;  // IN/OUTは2アドレス
            else offset += 1;
        }
    }

    // ---- 第2パス: 機械語生成 ----

    for (auto& line : lines) {
        const std::string& inst = line.inst;
        if (isAssemblerInst(inst)) {
            if (inst == "DC") {
                std::string cs;
                if (parseCharConst(line.operand, cs)) {
                    // 文字列: 1文字1ワード（COMET II仕様: DC 'A' = 0x0041）
                    for (size_t i = 0; i < cs.size(); i++) {
                        result.obj.push_back(static_cast<uint8_t>(cs[i]));
                    }
                } else {
                    // カンマ区切りの数値定数
                    std::istringstream ops(line.operand);
                    std::string tok;
                    while (std::getline(ops, tok, ',')) {
                        tok = trim(tok);
                        int val = 0;
                        if (!parseInt(tok, val)) {
                            result.error = "OPERAND ERROR: DCの定数が不正 '" + tok +
                                           "'（行" + std::to_string(line.lineNo) + "）";
                            return result;
                        }
                        result.obj.push_back(static_cast<uint16_t>(val));
                    }
                }
            } else if (inst == "DS") {
                int n = 0;
                parseInt(line.operand, n);
                for (int i = 0; i < n; i++) {
                    result.obj.push_back(0);
                }
            }
            continue;
        }

        if (isMacroInst(inst)) {
            // RPUSH: PUSH GR0〜GR4 / RPOP: POP GR4〜GR0
            if (inst == "RPUSH") {
                for (int g = 0; g <= 4; g++) {
                    result.obj.push_back(static_cast<uint16_t>((OP_PUSH << 8) | (g << 3)));
                }
            } else {
                for (int g = 4; g >= 0; g--) {
                    result.obj.push_back(static_cast<uint16_t>((OP_POP << 8) | (g << 3)));
                }
            }
            continue;
        }

        auto it = findInst(inst);
        if (it == nullptr) {
            result.error = "OP CODE ERROR: 不明な命令 '" + inst + "'（行" +
                           std::to_string(line.lineNo) + "）";
            return result;
        }
        uint8_t opcode = it->opcode;
        int type = it->type;

        // オペランドの解析
        int gr = 0, xr = 0;
        std::string adrStr, lenStr;


        if (type == 1) {  // grのみ（PUSH/POP）
            if (!parseGR(line.operand, gr)) {
                result.error = "OPERAND ERROR: GRの指定が不正 '" + line.operand +
                               "'（行" + std::to_string(line.lineNo) + "）";
                return result;
            }
        } else if (type == 2 || type == 4) {  // gr,adr[,xr] または gr,定数
            std::istringstream ops(line.operand);
            std::string gs, as;
            if (!std::getline(ops, gs, ',')) {
                result.error = "OPERAND ERROR: オペランドが不足（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            if (!parseGR(trim(gs), gr)) {
                result.error = "OPERAND ERROR: GRの指定が不正 '" + gs +
                               "'（行" + std::to_string(line.lineNo) + "）";
                return result;
            }
            std::getline(ops, as, ',');
            adrStr = trim(as);

            // 指標レジスタ（3つ目のオペランド）
            std::string xs;
            if (std::getline(ops, xs, ',')) {
                if (!parseGR(trim(xs), xr)) {
                    result.error = "OPERAND ERROR: XRの指定が不正 '" + xs +
                                   "'（行" + std::to_string(line.lineNo) + "）";
                    return result;
                }
            }
        } else if (type == 3 || type == 5) {  // adr[,xr] または adr,len
            std::istringstream ops(line.operand);
            std::string as;
            if (!std::getline(ops, as, ',')) {
                result.error = "OPERAND ERROR: アドレスが不足（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            adrStr = trim(as);

            // IN/OUTは2つ目のオペランド（len領域）を保持
            if (type == 5) {
                std::string xs;
                if (std::getline(ops, xs, ',')) {
                    lenStr = trim(xs);
                }
            } else {
                std::string xs;
                if (std::getline(ops, xs, ',')) {
                    if (!parseGR(trim(xs), xr)) {
                        result.error = "OPERAND ERROR: XRの指定が不正 '" + xs +
                                       "'（行" + std::to_string(line.lineNo) + "）";
                        return result;
                    }
                }
            }
        }

        // ワード1: [opcode][gr][xr]
        uint16_t w1 = static_cast<uint16_t>((opcode << 8) | (gr << 3) | xr);
        result.obj.push_back(w1);

        // ワード2: アドレス（アドレスを取る命令のみ）
        if (type == 2 || type == 3 || type == 4 || type == 5) {
            int addr = 0;
            if (type == 4) {
                // SLL/SRL: シフト量（定数）
                if (!parseInt(adrStr, addr) || addr < 0 || addr > 15) {
                    result.error = "OPERAND ERROR: シフト量が不正 '" + adrStr +
                                   "'（行" + std::to_string(line.lineNo) + "）";
                    return result;
                }
            } else {
                // ラベル or 定数
                if (!adrStr.empty() && labels.count(adrStr)) {
                    // オフセット（語単位）→ 語アドレス（OBJ_BASE + オフセット）
                    addr = OBJ_BASE + labels[adrStr];
                } else if (parseInt(adrStr, addr)) {
                    addr &= 0xFFFF;
                } else if (type == 5) {
                    // IN/OUTの領域はラベル必須
                    result.error = "LABEL ERROR: アドレス '" + adrStr +
                                   "' が見つからない（行" +
                                   std::to_string(line.lineNo) + "）";
                    return result;
                } else {
                    result.error = "LABEL ERROR: ラベル '" + adrStr +
                                   "' が見つからない（行" +
                                   std::to_string(line.lineNo) + "）";
                    return result;
                }
            }
            result.obj.push_back(static_cast<uint16_t>(addr));

            // ワード3: 第2アドレス（IN/OUTの文字長領域）
            if (type == 5) {
                int lenAddr = 0;
                if (labels.count(lenStr)) {
                    lenAddr = OBJ_BASE + labels[lenStr];
                } else if (parseInt(lenStr, lenAddr)) {
                    lenAddr &= 0xFFFF;
                } else {
                    result.error = "LABEL ERROR: 長さ領域 '" + lenStr +
                                   "' が見つからない（行" +
                                   std::to_string(line.lineNo) + "）";
                    return result;
                }
                result.obj.push_back(static_cast<uint16_t>(lenAddr));
            }
        }
    }

    result.ok = true;
    result.obj_words = static_cast<int>(result.obj.size());
    return result;
}

} // namespace casl
