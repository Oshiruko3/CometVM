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
// tca8418.h - TCA8418キーボードコントローラの直接駆動（Cardputer ADV版用）
// ポケコン復活プロジェクト フェーズ1-第2段階 ②（UI層）
//
// M5Unified非依存。Arduino Wire で I2C を直接操作する。
// ピン: SDA=8, SCL=9（Cardputer ADVの内部I2C・M5Unifiedのピンテーブルより）
// レジスタ定義は Adafruit_TCA8418_registers.h（BSDライセンス）を流用。

#pragma once

#include <Arduino.h>
#include <Wire.h>

class TCA8418 {
public:
    static constexpr uint8_t I2C_ADDR = 0x34;   ///< TCA8418のI2Cアドレス
    static constexpr int PIN_SDA = 8;
    static constexpr int PIN_SCL = 9;
    static constexpr int PIN_INT = 11;          ///< 割り込みピン（今回はポーリングで不使用）

    // レジスタ（Adafruit_TCA8418_registers.h より）
    static constexpr uint8_t REG_CFG = 0x01;
    static constexpr uint8_t REG_INT_STAT = 0x02;
    static constexpr uint8_t REG_KEY_LCK_EC = 0x03;   // 下位4bit = キーイベント数
    static constexpr uint8_t REG_KEY_EVENT_A = 0x04;  // FIFO先頭イベント
    static constexpr uint8_t REG_KP_GPIO_1 = 0x1D;    // 行マスク
    static constexpr uint8_t REG_KP_GPIO_2 = 0x1E;    // 列マスク（0-7）
    static constexpr uint8_t REG_KP_GPIO_3 = 0x1F;    // 列マスク（8-9）
    static constexpr uint8_t REG_GPIO_DIR_1 = 0x23;
    static constexpr uint8_t REG_GPIO_DIR_2 = 0x24;
    static constexpr uint8_t REG_GPIO_DIR_3 = 0x25;
    static constexpr uint8_t REG_GPI_EM_1 = 0x20;
    static constexpr uint8_t REG_GPI_EM_2 = 0x21;
    static constexpr uint8_t REG_GPI_EM_3 = 0x22;
    static constexpr uint8_t REG_GPIO_INT_LVL_1 = 0x26;
    static constexpr uint8_t REG_GPIO_INT_LVL_2 = 0x27;
    static constexpr uint8_t REG_GPIO_INT_LVL_3 = 0x28;
    static constexpr uint8_t REG_GPIO_INT_EN_1 = 0x1A;
    static constexpr uint8_t REG_GPIO_INT_EN_2 = 0x1B;
    static constexpr uint8_t REG_GPIO_INT_EN_3 = 0x1C;

    // キーイベント（Cardputerの座標系にリマップ済み）
    // row: 0-3, col: 0-13（M5Cardputerの_key_value_mapと同じ座標）
    struct KeyEvent {
        uint8_t row = 0;
        uint8_t col = 0;
        bool pressed = false;  // true=押下, false=離し
    };

    bool begin();
    bool hasEvent();                        // FIFOにイベントがあるか
    bool readEvent(KeyEvent& ev);           // 1イベント読み出し（無ければfalse）
    uint8_t eventCount();                   // 待機中のイベント数

private:
    uint8_t readReg(uint8_t reg);
    bool writeReg(uint8_t reg, uint8_t val);

    // TCA8418の(row, col) → Cardputerの(row, col) へのリマップ
    // （M5CardputerのTCA8418KeyboardReader::remapと同じ式）
    void remap(uint8_t& row, uint8_t& col);
};
