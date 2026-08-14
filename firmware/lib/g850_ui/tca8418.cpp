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
// tca8418.cpp - TCA8418キーボードコントローラの直接駆動（Cardputer ADV版用）
// 初期化・イベント読み出しは Adafruit_TCA8418 + M5Cardputer の実装を
// Wire ベースに簡略化したもの（両者ともMIT/BSDライセンス）。

#include "tca8418.h"

bool TCA8418::begin()
{
    Wire.begin(PIN_SDA, PIN_SCL);

    // デバイス確認（KEY_LCK_EC が読めれば応答あり）
    Wire.beginTransmission(I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    // ---- 初期化（Adafruit_TCA8418::begin と同一のレジスタ設定） ----
    // GPIOをすべて入力に
    writeReg(REG_GPIO_DIR_1, 0x00);
    writeReg(REG_GPIO_DIR_2, 0x00);
    writeReg(REG_GPIO_DIR_3, 0x00);
    // 全ピンをキーイベントモードに
    writeReg(REG_GPI_EM_1, 0xFF);
    writeReg(REG_GPI_EM_2, 0xFF);
    writeReg(REG_GPI_EM_3, 0xFF);
    // フォーリングエッジ割り込み
    writeReg(REG_GPIO_INT_LVL_1, 0x00);
    writeReg(REG_GPIO_INT_LVL_2, 0x00);
    writeReg(REG_GPIO_INT_LVL_3, 0x00);
    // 全ピンの割り込み有効
    writeReg(REG_GPIO_INT_EN_1, 0xFF);
    writeReg(REG_GPIO_INT_EN_2, 0xFF);
    writeReg(REG_GPIO_INT_EN_3, 0xFF);

    // ---- キーパッドマトリクス: 7行×8列（M5Cardputer::matrix(7, 8)） ----
    // KP_GPIO_1: R0〜R6 をキーパッド行に
    writeReg(REG_KP_GPIO_1, 0x7F);
    // KP_GPIO_2: C0〜C7 をキーパッド列に
    writeReg(REG_KP_GPIO_2, 0xFF);
    // KP_GPIO_3: 列8以降は使わない
    writeReg(REG_KP_GPIO_3, 0x00);

    // ---- キーイベント割り込みの有効化（enableInterrupts 相当） ----
    // CFG: KE_IEN(bit0) | GPI_IEN(bit1)
    uint8_t cfg = readReg(REG_CFG);
    cfg |= 0x03;
    writeReg(REG_CFG, cfg);

    return true;
}

uint8_t TCA8418::eventCount()
{
    return readReg(REG_KEY_LCK_EC) & 0x0F;
}

bool TCA8418::hasEvent()
{
    return eventCount() > 0;
}

bool TCA8418::readEvent(KeyEvent& ev)
{
    if (!hasEvent()) {
        return false;
    }

    // FIFO先頭のイベントを読む（読むとFIFOから消える）
    uint8_t raw = readReg(REG_KEY_EVENT_A);
    ev.pressed = (raw & 0x80) != 0;

    // キー番号（1-based: 1〜80）→ 0-based の (row, col)
    uint16_t key = (raw & 0x7F) - 1;
    uint8_t row = key / 10;
    uint8_t col = key % 10;
    remap(row, col);
    ev.row = row;
    ev.col = col;

    // 割り込みフラグのクリア（KEY_EVENT_INT=bit0）
    writeReg(REG_INT_STAT, 0x01);
    return true;
}

uint8_t TCA8418::readReg(uint8_t reg)
{
    Wire.beginTransmission(I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;
}

bool TCA8418::writeReg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void TCA8418::remap(uint8_t& row, uint8_t& col)
{
    // TCA8418の物理座標 → Cardputerの論理座標（M5Cardputerと同じ変換）
    uint8_t new_col = row * 2;
    if (col > 3) {
        new_col++;
    }
    row = (col + 4) % 4;
    col = new_col;
}
