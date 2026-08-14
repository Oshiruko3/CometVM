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
// COMET VM - コマンドライン実行ツール
// 使い方: cometvm <sample.cas>
//   学習用サンプルをアセンブルして実行し、OUTの出力を表示します。
//   実行はトップレベルの RET（SP==0 のときの RET）で終了します。
//   入力（IN命令）は空入力として扱います。

#include "casl_asm.h"
#include "comet.h"
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

static void outFunc(char c) { std::putchar(c); }
static int inFunc(char* /*buf*/, int /*maxLen*/) { return 0; }

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "使い方: cometvm <file.cas>\n");
        return 1;
    }
    std::ifstream f(argv[1]);
    if (!f) {
        std::fprintf(stderr, "ファイルを開けない: %s\n", argv[1]);
        return 1;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    auto res = casl::assemble(ss.str());
    if (!res.ok) {
        std::fprintf(stderr, "アセンブル失敗: %s\n", res.error.c_str());
        return 1;
    }
    auto c = std::make_unique<comet::Comet>();
    comet::reset(*c);
    c->out_func = outFunc;
    c->in_func = inFunc;
    for (int i = 0; i < res.obj_words; i++) c->writeWord(0x1000 + i, res.obj[i]);
    for (int i = 0; i < 10000; i++) {
        uint16_t opw = c->readWord(c->PC);
        if (static_cast<uint8_t>(opw >> 8) == comet::OP_RET && c->SP == 0) break;
        comet::step(*c);
    }
    std::putchar('\n');
    return 0;
}
