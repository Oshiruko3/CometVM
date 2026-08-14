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
// LGFX_Cardputer.h - CardputerのTFT（ST7789）をLovyanGFXで直接駆動する設定
// ポケコン復活プロジェクト フェーズ1-第2段階 ②（UI層）
//
// M5Unified/M5Cardputer/M5GFX を使わない（8/12調査: フリーズの原因）。
// ピン配線は M5GFX の自動検出コード（M5GFX.cpp 2293行付近）から抽出:
//   - SPI: MOSI=35, SCLK=36, DC=34, CS=37, RST=33（SPI3_HOST・40MHz）
//   - バックライト: PWM GPIO38（256Hz）
//   - パネル: ST7789, フレームメモリ内の有効領域 135x240 @ (52,40)、invert
//   - 回転1で 240x135（横長）

#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX(void) {
        // SPIバス設定（M5GFXのCardputer検出コードと同一）
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_mode = 0;
            cfg.pin_sclk = 36;
            cfg.pin_mosi = 35;
            cfg.pin_miso = -1;  // 未接続（書き込み専用）
            cfg.pin_dc = 34;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // ST7789パネル設定
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 37;
            cfg.pin_rst = 33;
            cfg.panel_width = 135;   // ガラスの有効表示領域（ドライバIC内）
            cfg.panel_height = 240;
            cfg.offset_x = 52;       // ST7789フレームメモリ内のオフセット
            cfg.offset_y = 40;
            cfg.offset_rotation = 0;
            cfg.invert = true;       // ST7789は通常invertが必要
            cfg.readable = false;
            _panel.config(cfg);
        }

        // バックライト（PWM）
        {
            auto cfg = _light.config();
            cfg.pin_bl = 38;
            cfg.invert = false;
            cfg.freq = 256;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
        setRotation(1);  // 240x135 横長
    }
};
