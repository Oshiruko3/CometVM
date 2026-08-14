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
// kbd.h - Cardputerのキーボード（TCA8418）を文字に変換する薄い層
// ポケコン復活プロジェクト フェーズ1-第2段階 ②（UI層）
//
// M5CardputerのKeyboardクラス相当を、M5Unified非依存で実装し直したもの。
// キーイベント（押下/離し）→ モディファイア状態更新 → 文字コード出力。

#pragma once

#include <Arduino.h>
#include "tca8418.h"

class CardputerKbd {
public:
    // TCA8418初期化（Wire: SDA=8, SCL=9）
    bool begin();

    // 押下イベントがあれば1文字返す（無ければfalse）
    // モディファイア（SHIFT等）の状態更新も行う
    bool readChar(uint8_t& ch);

    bool isShiftHeld() const { return _shift; }
    bool isCtrlHeld() const { return _ctrl; }
    bool isFnHeld() const { return _fn; }

private:
    TCA8418 _tca;
    bool _shift = false;
    bool _ctrl = false;
    bool _alt = false;
    bool _fn = false;
};
