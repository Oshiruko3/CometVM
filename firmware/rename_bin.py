# COMET VM - CASL II 仮想マシン（COMET II エミュレータ + CASL2 アセンブラ）
# Copyright (C) 2026 K. Matsumoto
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# rename_bin.py - ビルド後にファームウェアを com-vm.bin としてコピーする
# M5Launcherでの表示名を「COMET VM」にするため（共同開発時に確認した要望・8/13）
# platformio.ini の extra_scripts から呼ばれる

Import("env")
import shutil

def rename_bin(source, target, env):
    # PlatformIOのバージョンによって引数の順序が異なるため、両方を確認する
    candidates = [str(x) for x in list(source) + list(target)]
    src = next((c for c in candidates if "firmware.bin" in c), None)
    if src is None:
        print("### firmware.bin が見つかりません")
        return
    dst = src.replace("firmware.bin", "comet-vm.bin")
    shutil.copy(src, dst)
    print(f"M5Launcher用: {dst} を作成しました")

env.AddPostAction("buildprog", rename_bin)
