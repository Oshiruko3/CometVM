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
    {"CPA",  OP_CPA,  2}, {"CPL", OP_CPL, 2},
    {"SLL",  OP_SLL,  4}, {"SLA", OP_SLA, 4}, {"SRL", OP_SRL, 4}, {"SRA", OP_SRA, 4},  // SLA=算術左シフト（ビット15保持・8/13修正）・SRA=算術右シフト（8/13追加）
    {"JMP",  OP_JMP,  3}, {"JUMP", OP_JMP, 3},  // JUMPはJMPの別名（t10.casの記法）
    {"JPZ",  OP_JPZ,  3}, {"JZE", OP_JZE, 3}, {"JNZ", OP_JNZ, 3},  // JZEは独立命令（8/13修正: 従来JPZの別名だった誤り）
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
    // CRLF対応: 行末の\rを除去（8/13追加: 共同開発時に確認したサンプルがWindows改行）
    std::string raw2 = raw;
    if (!raw2.empty() && raw2.back() == '\r') raw2.pop_back();
    // コメント除去
    std::string s = raw2;
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
        if (remaining.empty()) return true;  // ラベルのみの行（8/13修正: ラベル定義として有効——共同開発時に確認したtest01）
        std::istringstream iss2(remaining);
        iss2 >> line.inst;
        std::getline(iss2, rest);
    } else if (isInst(t1)) {
        // 最初のトークンが命令コード——でも、次のトークンも命令なら、
        // t1は命令名と同名のラベル（例: t03.casの "AND   START"）
        std::string t2;
        {
            std::istringstream iss2(remaining);
            iss2 >> t2;
        }

        if (!t2.empty()) {
            // 命令名と同名のラベルの判定を厳密化（8/13修正: t10.casのJPL ENDを誤解釈）
            bool t1m = findInst(upper(t1)) != nullptr;   // t1が機械語命令
            bool t2m = findInst(upper(t2)) != nullptr;   // t2が機械語命令
            if ((t1m && upper(t2) == "START") ||  // AND START: t1はラベル（t03.cas）
                (isMacroInst(upper(t1)) && upper(t2) == "START") ||  // RPUSH START: t1はラベル（rpush.cas・8/13追加。マクロ命令名と同名のラベル）
                (t1m && (upper(t2) == "DC" || upper(t2) == "DS")) ||  // ST DC: t1はラベル（a01.cas・8/13追加。命令+DC/DSの組み合わせは存在しないため曖昧さなし）
                ((upper(t1) == "RET" || upper(t1) == "NOP") && (upper(t2) == "RET" || upper(t2) == "NOP")) ||  // RET RET: t1はラベル（c01.cas・8/13追加。t1自体がオペランドなし命令の場合のみ——JZE RETは巻き込まない）
                (upper(t1) == "END") ||           // END RPOP: t1はラベル（c11.cas・8/13追加。ENDはオペランドを取らない疑似命令——JPL ENDはt1=JPLなので影響なし）
                (!t1m && t2m)) {                  // END RET: t1はラベル（t10.cas）
                line.label = t1;
                remaining = trim(remaining);
                std::istringstream iss3(remaining);
                iss3 >> line.inst;
                std::getline(iss3, rest);
            } else {
                line.inst = t1;  // JPL END等: t1は命令（ENDはオペランド）
                rest = remaining;
            }
        } else {
            line.inst = t1;
            rest = remaining;
        }
    } else {
        // 「:」なしのラベル（例: "X    DC 5"）
        line.label = t1;
        remaining = trim(remaining);
        if (remaining.empty()) return true;  // ラベルのみの行（8/13修正: ラベル定義として有効）
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

// DCオペランドを解析してワード列を返す（成功時true）
// 'A'形式の文字定数・'ABC'形式の文字列・数値定数・カンマ区切りに対応
// ※8/13修正: カンマ区切りの文字定数リスト（'G','U',...）が、旧parseCharConstで
// オペランドをカンマ分割（引用符内のカンマは分割しない・8/13修正: c05.casの=','）
static std::vector<std::string> splitOperand(const std::string& s) {
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuote = false;
    for (char ch : s) {
        if (ch == '\'') inQuote = !inQuote;
        if (ch == ',' && !inQuote) {
            tokens.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    tokens.push_back(cur);
    return tokens;
}

//   引用符・カンマごと1つの文字列として誤解析されるバグを修正（test_evenoddで発見）
static bool parseDC(const std::string& operand, std::vector<uint16_t>& words, std::string& err,
                   const std::map<std::string, int>& labels, bool resolveLabels) {
    // カンマ分割（引用符内のカンマは分割しない・8/13修正: c05.casの'21,569,1387'）
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuote = false;
    for (char ch : operand) {
        if (ch == '\'') inQuote = !inQuote;
        if (ch == ',' && !inQuote) {
            tokens.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    tokens.push_back(cur);
    for (auto& tok : tokens) {
        tok = trim(tok);
        if (tok.empty()) continue;
        if (tok.size() >= 2 && tok.front() == '\'' && tok.back() == '\'') {
            // 文字定数（'A' = 1文字・'ABC' = 複数文字を1文字ずつ）
            std::string inner = tok.substr(1, tok.size() - 2);
            for (char c : inner) {
                words.push_back(static_cast<uint8_t>(c));
            }
        } else if (labels.count(tok)) {
            // ラベル参照（LAST DC LAST 等）: ラベルのアドレス値（8/13追加・t15.cas）
            words.push_back(OBJ_BASE + labels.at(tok));
        } else {
            // 数値定数
            int val = 0;
            if (parseInt(tok, val)) {
                words.push_back(static_cast<uint16_t>(val));
            } else if (!resolveLabels) {
                words.push_back(0);  // 第1パス: 後方参照のラベルは未登録——0を仮配置（サイズ計算のみ）
            } else {
                err = tok;
                return false;
            }
        }
    }
    if (words.empty()) {
        err = operand;
        return false;
    }
    return true;
}

// ---- アセンブラ本体 ----
AsmResult assemble(const std::string& source) {
    AsmResult result;
    std::vector<Line> lines;
    // 8/14修正: ラベルはプログラム（START〜END）ごとにスコープされる（CASL2仕様）
    // 区間ごとのラベルマップ + 入口名（STARTラベル・他区間から参照可能）
    std::vector<std::map<std::string, int>> sectionLabels;
    std::map<std::string, int> entryNames;
    int currentSection = -1;  // STARTで区間開始（test10の複数プログラム対応）
    // リテラル（=定数）: 出現順のキーリストと、キー → 相対オフセット（8/13追加）
    std::vector<std::string> literalKeys;
    std::map<std::string, int> literalPos;

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
        const std::string& inst = line.inst;
        if (inst == "START") {
            // 新しいプログラム（区間）の開始——8/14修正: ラベルのスコープを区間ごとに
            currentSection++;
            sectionLabels.push_back({});
        }
        if (!line.label.empty()) {
            if (currentSection < 0) {
                result.error = "LABEL ERROR: START前にラベル '" + line.label + "'（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            if (sectionLabels[currentSection].count(line.label)) {
                result.error = "LABEL ERROR: ラベル '" + line.label + "' が重複（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            sectionLabels[currentSection][line.label] = offset;
            if (inst == "START") {
                // STARTのラベルは入口名として他区間から参照可能（CASL2仕様）
                entryNames[line.label] = offset;
            }
        }
        if (inst.empty()) continue;  // ラベルのみの行（8/13修正: サイズ0）
        if (isAssemblerInst(inst)) {
            if (inst == "DC") {
                // parseDCでワード数を計算（文字定数・数値定数・カンマ区切りに対応）
                std::vector<uint16_t> words;
                std::string err;
                if (!parseDC(line.operand, words, err, sectionLabels[currentSection], false)) {
                    result.error = "OPERAND ERROR: DCの定数が不正 '" + err + "'（行" +
                                   std::to_string(line.lineNo) + "）";
                    return result;
                }
                offset += static_cast<int>(words.size());
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
            offset += 7;  // RPUSH/RPOPは7命令に展開（GR1〜GR7・8/13修正: 従来5命令と誤り）
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

            // リテラル（=定数）の収集（命令のオペランドから・8/13修正: splitOperandで引用符内カンマ対応）
            for (auto& tok2 : splitOperand(line.operand)) {
                tok2 = trim(tok2);
                if (tok2.size() >= 2 && tok2[0] == '=') {
                    std::string key = tok2.substr(1);
                    if (!literalPos.count(key)) literalKeys.push_back(key);
                }
            }
        }
    }

    // リテラル（=定数）の配置位置を決定（プログラム全体の後に配置）
    int literalBase = offset;
    for (size_t i = 0; i < literalKeys.size(); i++) {
        literalPos[literalKeys[i]] = literalBase + static_cast<int>(i);
    }
    offset += static_cast<int>(literalKeys.size());

    // ---- 第2パス: 機械語生成 ----

    // ---- 第2パス: コード生成 ----
    int section = -1;
    for (auto& line : lines) {
        const std::string& inst = line.inst;
        if (inst == "START") {
            section++;
            if (section >= static_cast<int>(sectionLabels.size())) sectionLabels.push_back({});
        }
        if (inst.empty()) continue;  // ラベルのみの行（8/13修正: コード生成なし）
        // 参照解決用: 現在の区間のラベル + 入口名（8/14修正: ラベルスコープ対応）
        std::map<std::string, int> visibleLabels =
            (section >= 0) ? sectionLabels[section] : std::map<std::string, int>{};
        for (auto& [name, addr] : entryNames) {
            if (!visibleLabels.count(name)) visibleLabels[name] = addr;
        }
        if (isAssemblerInst(inst)) {
            if (inst == "DC") {
                std::vector<uint16_t> words;
                std::string err;
                if (!parseDC(line.operand, words, err, visibleLabels, true)) {
                    result.error = "OPERAND ERROR: DCの定数が不正 '" + err + "'（行" +
                                   std::to_string(line.lineNo) + "）";
                    return result;
                }
                for (auto w : words) {
                    result.obj.push_back(w);
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
            // RPUSH: PUSH GR1〜GR7（GR0は戻り値用で対象外・8/13修正） / RPOP: POP GR7〜GR1
            if (inst == "RPUSH") {
                for (int g = 1; g <= 7; g++) {
                    result.obj.push_back(static_cast<uint16_t>((OP_PUSH << 8) | (g << 4)));
                }
            } else {
                for (int g = 7; g >= 1; g--) {
                    result.obj.push_back(static_cast<uint16_t>((OP_POP << 8) | (g << 4)));
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
            // 「PUSH 0,GR1」形式（a04.cas: アドレス0,指標GR1の記法）にも対応——カンマ分割してGRを検出
            bool foundGR = false;
            for (auto& tok1 : splitOperand(line.operand)) {
                if (parseGR(trim(tok1), gr)) { foundGR = true; break; }
            }
            if (!foundGR) {
                result.error = "OPERAND ERROR: GRの指定が不正 '" + line.operand +
                               "'（行" + std::to_string(line.lineNo) + "）";
                return result;
            }
        } else if (type == 2 || type == 4) {  // gr,adr[,xr] または gr,定数
            auto ops = splitOperand(line.operand);
            if (ops.size() < 2 || !parseGR(trim(ops[0]), gr)) {
                result.error = "OPERAND ERROR: オペランドが不足（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            adrStr = trim(ops[1]);

            // 指標レジスタ（3つ目のオペランド）
            if (ops.size() >= 3) {
                if (!parseGR(trim(ops[2]), xr)) {
                    result.error = "OPERAND ERROR: XRの指定が不正 '" + ops[2] +
                                   "'（行" + std::to_string(line.lineNo) + "）";
                    return result;
                }
            }
        } else if (type == 3 || type == 5) {  // adr[,xr] または adr,len
            auto ops = splitOperand(line.operand);
            if (ops.size() < 1) {
                result.error = "OPERAND ERROR: アドレスが不足（行" +
                               std::to_string(line.lineNo) + "）";
                return result;
            }
            adrStr = trim(ops[0]);

            // IN/OUTは2つ目のオペランド（len領域）を保持
            if (type == 5) {
                if (ops.size() >= 2) {
                    lenStr = trim(ops[1]);
                }
            } else {
                if (ops.size() >= 2) {
                    if (!parseGR(trim(ops[1]), xr)) {
                        result.error = "OPERAND ERROR: XRの指定が不正 '" + ops[1] +
                                       "'（行" + std::to_string(line.lineNo) + "）";
                        return result;
                    }
                }
            }
        }

        // LD gr,gr（レジスタ間転送）→ ワード2を 0xFF00|gr2 形式に（8/13修正: b07.casで発見）
        // ※従来は「LAD gr,0,gr2 変換」だったが、GR0は指標レジスタにできないため
        //   「LD GR5,GR0」が xr=0 になり GR5=0 に壊れていた。
        //   0xFF00|gr2 形式は VM の getOperand がレジスタ間として解釈する（GR0でも動く）
        if (opcode == OP_LD && !adrStr.empty()) {
            int reg2 = 0;
            if (parseGR(adrStr, reg2)) {
                char buf[16];
                snprintf(buf, sizeof(buf), "#FF%02X", reg2);
                adrStr = buf;  // ワード2 = 0xFF00 | reg2（レジスタ間指定）
            }
        }

        // ワード1: [opcode][gr][xr]（8/14修正: GRはbit7-4・XRはbit3-0の8-4-4形式——test14の#1070対応）
        uint16_t w1 = static_cast<uint16_t>((opcode << 8) | (gr << 4) | xr);
        result.obj.push_back(w1);

        // ワード2: アドレス（アドレスを取る命令のみ）
        if (type == 2 || type == 3 || type == 4 || type == 5) {
            int addr = 0;
            // 8/13修正: シフト量も実行アドレス（adr + GR[xr]）——type 4も共通の解決に
            // （従来は定数のみ・c10.casのSRA GR0,-1,GR2が不正とされた）
            {
                // ラベル・定数・リテラル
                if (adrStr.empty()) {
                    addr = 0;  // レジスタ間転送（LAD gr,0,gr2 変換）の場合
                } else if (adrStr[0] == '=') {
                    // リテラル（=定数）: DCを自動配置した番地を参照
                    std::string key = adrStr.substr(1);
                    if (literalPos.count(key)) {
                        addr = OBJ_BASE + literalPos[key];
                    } else {
                        result.error = "LITERAL ERROR: リテラル '" + adrStr +
                                       "' が見つからない（行" +
                                       std::to_string(line.lineNo) + "）";
                        return result;
                    }
                } else if (!adrStr.empty() && visibleLabels.count(adrStr)) {
                    // オフセット（語単位）→ 語アドレス（OBJ_BASE + オフセット）
                    addr = OBJ_BASE + visibleLabels[adrStr];
                } else {
                    // ラベル±定数（例: TARGET_INSTR+1・8/14追加: 共同開発時に確認したtest14自己書き換え）
                    // → 数値 → レジスタ間指定の順に解決
                    bool resolved = false;
                    size_t i = 0;
                    while (i < adrStr.size() &&
                           (isalpha(adrStr[i]) || isdigit(adrStr[i]) || adrStr[i] == '_')) {
                        i++;
                    }
                    if (i > 0 && i < adrStr.size()) {
                        std::string labelPart = adrStr.substr(0, i);
                        std::string rest = adrStr.substr(i);
                        if (!rest.empty() && rest[0] == '+') rest = rest.substr(1);
                        int delta = 0;
                        if (visibleLabels.count(labelPart) && parseInt(rest, delta)) {
                            addr = OBJ_BASE + visibleLabels[labelPart] + delta;
                            resolved = true;
                        }
                    }
                    if (!resolved && parseInt(adrStr, addr)) {
                        addr &= 0xFFFF;
                        resolved = true;
                    }
                    if (!resolved) {
                        // レジスタ間指定（op gr1,gr2形式）→ 0xFF00 | GR番号
                        // ※VMが getOperand でレジスタ間演算として解釈（8/13追加）
                        int reg2 = 0;
                        if (parseGR(adrStr, reg2)) {
                            addr = 0xFF00 | reg2;
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
                }
            }
            result.obj.push_back(static_cast<uint16_t>(addr));

            // ワード3: 第2アドレス（IN/OUTの文字長領域）
            if (type == 5) {
                int lenAddr = 0;
                if (visibleLabels.count(lenStr)) {
                    lenAddr = OBJ_BASE + visibleLabels[lenStr];
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

    // リテラル（=定数）をオブジェクト末尾に配置（数値・16進・文字定数に対応）
    for (auto& key : literalKeys) {
        int val = 0;
        if (key.size() == 3 && key.front() == '\'' && key.back() == '\'') {
            val = static_cast<uint8_t>(key[1]);  // 文字定数（'5' → 0x35）
        } else if (!parseInt(key, val)) {
            result.error = "LITERAL ERROR: リテラルの値が不正 '" + key + "'";
            return result;
        }
        result.obj.push_back(static_cast<uint16_t>(val));
    }

    result.ok = true;
    result.obj_words = static_cast<int>(result.obj.size());
    return result;
}

} // namespace casl
