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
// CASLアセンブラ - インターフェース
// ソーステキスト → COMET機械語（オブジェクト）への変換
// 設計: docs/comet_vm_design.md を参照
//
// 対応する構文:
//   [ラベル] [命令コード] [オペランド] [;コメント]
// 命令:
//   アセンブラ命令: START [番地] / END / DC 定数[,定数]... / DS 語数
//   マクロ命令:    IN 領域,長さ / OUT 領域,長さ / RPUSH / RPOP
//   機械語命令:    LD/LAD/ST/ADDA/ADDL/SUBA/SUBL/AND/OR/XOR/CPA gr,adr[,xr]
//                  SLL/SRL gr,定数
//                  JMP/JPZ/JNZ/JMI/JPL/JOV/CALL adr[,xr]
//                  PUSH/POP gr / RET / NOP

#ifndef CASL_ASM_H
#define CASL_ASM_H

#include <string>
#include <vector>

namespace casl {

// アセンブルの結果
struct AsmResult {
    bool ok = false;               // 成功したか
    std::string error;             // エラーメッセージ（失敗時）
    std::vector<uint16_t> obj;     // オブジェクト（#1000番地から配置するワード列）
    int obj_words = 0;             // オブジェクトのワード数
};

// CASLソースをアセンブルする
// source: CASLソース（行区切り）
// 戻り値: AsmResult（ok=trueならobjに機械語が入る）
AsmResult assemble(const std::string& source);

} // namespace casl

#endif // CASL_ASM_H
