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
// kbd.cpp - Cardputerのキーボード（TCA8418）を文字に変換する薄い層
// キーマップは M5Cardputer の Keyboard.h（MITライセンス）から移植。

#include "kbd.h"

// 特殊キーコード（M5CardputerのKeyboard_def.hと同一値）
static constexpr uint8_t KEY_LEFT_CTRL = 0x80;
static constexpr uint8_t KEY_LEFT_SHIFT = 0x81;
static constexpr uint8_t KEY_LEFT_ALT = 0x82;
static constexpr uint8_t KEY_FN = 0xFF;
static constexpr uint8_t KEY_OPT = 0x00;
static constexpr uint8_t KEY_BACKSPACE = 0x2A;
static constexpr uint8_t KEY_TAB = 0x2B;
static constexpr uint8_t KEY_ENTER = 0x28;

// キー配置（リマップ済み row 0-3 × col 0-13）
//   0: ` 1 2 3 4 5 6 7 8 9 0 - = [BS]
//   1: [TAB] q w e r t y u i o p [ ] \
//   2: [FN] [SHIFT] a s d f g h j k l ; ' [ENTER]
//   3: [CTRL] [OPT] [ALT] z x c v b n m , . / [SPACE]
static const uint8_t kKeyFirst[4][14] = {
    {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', KEY_BACKSPACE},
    {KEY_TAB, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
    {KEY_FN, KEY_LEFT_SHIFT, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', KEY_ENTER},
    {KEY_LEFT_CTRL, KEY_OPT, KEY_LEFT_ALT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '},
};

// SHIFT同時押し時（大文字・記号）
static const uint8_t kKeySecond[4][14] = {
    {'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', KEY_BACKSPACE},
    {KEY_TAB, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|'},
    {KEY_FN, KEY_LEFT_SHIFT, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', KEY_ENTER},
    {KEY_LEFT_CTRL, KEY_OPT, KEY_LEFT_ALT, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', ' '},
};

bool CardputerKbd::begin()
{
    return _tca.begin();
}

bool CardputerKbd::readChar(uint8_t& ch)
{
    TCA8418::KeyEvent ev;
    while (_tca.readEvent(ev)) {
        uint8_t first = kKeyFirst[ev.row][ev.col];

        // モディファイアキーは状態更新のみ（文字は返さない）
        switch (first) {
            case KEY_LEFT_SHIFT:
                _shift = ev.pressed;
                continue;
            case KEY_LEFT_CTRL:
                _ctrl = ev.pressed;
                continue;
            case KEY_LEFT_ALT:
                _alt = ev.pressed;
                continue;
            case KEY_OPT:
                continue;
            case KEY_FN:
                // FN押下は 0xFF として返す（段階2-3: モード切替=MODEキー相当）
                _fn = ev.pressed;
                if (ev.pressed) {
                    ch = 0xFF;
                    return true;
                }
                continue;
            default:
                break;
        }

        // 押下イベントで1文字
        if (ev.pressed) {
            ch = _shift ? kKeySecond[ev.row][ev.col] : first;
            return true;
        }
    }
    return false;
}
