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
// COMET VM on Cardputer - 段階2-3: G850風モードUI（PRO/ASM/RUN）
// ポケコン復活プロジェクト フェーズ1-第2段階 ②-③
//
// G850のMODEキー方式のUIを再現:
//   PRO（CASL2エディタ）→ FN(MODE) → ASM（アセンブル）→ FN → RUN（実行）→ FN → PRO
// 画面: 1行目=モード表示・2〜14行目=テキストウィンドウ・15行目=メッセージ
// 設計: docs/design_notes.md「段階2-3 エディタ仕様」参照

#include <Arduino.h>
#include "LGFX_Cardputer.h"
#include "kbd.h"
#include "casl_asm.h"
#include "comet.h"

static LGFX lcd;
static CardputerKbd kbd;

// ---- 画面定義（AsciiFont8x16・固定幅・240x135・G850の24文字x6行を再現） ----
// 共同開発時に確認した設計: 6行が規定・横24文字を基準・余白を2で割って中央寄せ
// AsciiFont8x16（FixedBMPfont・固定幅）——可変幅の問題（24文字でも幅が広がる）を解消:
// 24文字x8px=192px<240px（右端見切れなし）・6行x16px=96px<135px（6行収まる）
static constexpr int kCharW = 8;    // 文字幅（AsciiFont8x16・固定幅）
static constexpr int kCharH = 16;   // 文字高（AsciiFont8x16）
static constexpr int kCols = 24;    // 1行の文字数（G850の24文字が基準）
static constexpr int kWinRows = 4;  // テキストウィンドウ（2〜5行目）
static constexpr int kMsgRow = 5;   // メッセージ行（6行目・0-based）

// ---- モード（G850のMODEキーで順に切り替え） ----
enum class Mode { PRO, ASM, RUN };
static Mode g_mode = Mode::PRO;

// ---- テキストバッファ（CASL2ソース） ----
static constexpr int kBufSize = 1024;
static char g_buf[kBufSize];
static int g_bufLen = 0;
static int g_cursor = 0;  // バッファ内カーソル位置
static int g_scroll = 0;  // 表示開始行（スクロール）

// ---- アセンブル結果（ASM/RUNモードで使用） ----
static casl::AsmResult g_asm;

// ---- OUT出力バッファ（RUNモード） ----
static char g_outBuf[512];
static int g_outLen = 0;

// ---- メッセージ行 ----
static char g_msg[64] = "Ready.";  // メッセージ行（6行目）·ヒントは7行目の専用バー

// ============================================================
// バッファ操作
// ============================================================

// バッファ内位置posの行番号（0-based）
static int bufRowOf(int pos)
{
    int row = 0;
    for (int i = 0; i < pos && i < g_bufLen; i++) {
        if (g_buf[i] == '\n') row++;
    }
    return row;
}

// バッファ内位置posの行内列位置
static int bufColOf(int pos)
{
    int col = 0;
    for (int i = pos - 1; i >= 0; i--) {
        if (g_buf[i] == '\n') break;
        col++;
    }
    return col;
}

// 行数
static int bufLineCount()
{
    int n = 1;
    for (int i = 0; i < g_bufLen; i++) {
        if (g_buf[i] == '\n') n++;
    }
    return n;
}

// バッファのn行目（0-based）を最大maxLen文字コピー（\0終端）
static void getLineText(int n, char* out, int maxLen)
{
    int pos = 0, lineNo = 0;
    while (pos < g_bufLen && lineNo < n) {
        if (g_buf[pos] == '\n') lineNo++;
        pos++;
    }
    int len = 0;
    while (pos < g_bufLen && g_buf[pos] != '\n' && len < maxLen - 1) {
        out[len++] = g_buf[pos++];
    }
    out[len] = '\0';
}

// カーソル位置に1文字挿入
static void bufInsert(char ch)
{
    if (g_bufLen >= kBufSize - 1) {
        snprintf(g_msg, sizeof(g_msg), "Buffer full");
        return;
    }
    for (int i = g_bufLen; i > g_cursor; i--) {
        g_buf[i] = g_buf[i - 1];
    }
    g_buf[g_cursor] = ch;
    g_bufLen++;
    g_cursor++;
}

// カーソル左の1文字を削除
static void bufBackspace()
{
    if (g_cursor <= 0) return;
    for (int i = g_cursor - 1; i < g_bufLen - 1; i++) {
        g_buf[i] = g_buf[i + 1];
    }
    g_bufLen--;
    g_cursor--;
}

// カーソルがウィンドウ内に見えるようスクロール調整
static void ensureCursorVisible()
{
    int row = bufRowOf(g_cursor);
    if (row < g_scroll) g_scroll = row;
    if (row >= g_scroll + kWinRows) g_scroll = row - kWinRows + 1;
    if (g_scroll < 0) g_scroll = 0;
}

// カーソルを含む行の先頭位置
static int lineStartOf(int pos)
{
    int p = pos;
    while (p > 0 && g_buf[p - 1] != '\n') p--;
    return p;
}

// カーソルを含む行の末尾位置（改行の直前）
static int lineEndOf(int pos)
{
    int p = pos;
    while (p < g_bufLen && g_buf[p] != '\n') p++;
    return p;
}

// ---- カーソル移動（FN+;,./ = 上下左右・Cardputerの標準割り当て） ----
static void drawScreen();  // 前方宣言（描画関数は後で定義）

static void moveCursorLeft()
{
    if (g_cursor > 0) {
        g_cursor--;
        ensureCursorVisible();
        drawScreen();
    }
}

static void moveCursorRight()
{
    if (g_cursor < g_bufLen) {
        g_cursor++;
        ensureCursorVisible();
        drawScreen();
    }
}

static void moveCursorUp()
{
    int col = bufColOf(g_cursor);
    int start = lineStartOf(g_cursor);
    if (start == 0) return;  // 先頭行
    int prevEnd = start - 1;  // 前の行の改行位置
    int prevStart = lineStartOf(prevEnd);
    int target = prevStart + col;
    int prevLineEnd = lineEndOf(prevStart);
    if (target > prevLineEnd) target = prevLineEnd;
    g_cursor = target;
    ensureCursorVisible();
    drawScreen();
}

static void moveCursorDown()
{
    int col = bufColOf(g_cursor);
    int end = lineEndOf(g_cursor);
    if (end >= g_bufLen) return;  // 最終行
    int nextStart = end + 1;  // 次の行の先頭
    int target = nextStart + col;
    int nextLineEnd = lineEndOf(nextStart);
    if (target > nextLineEnd) target = nextLineEnd;
    g_cursor = target;
    ensureCursorVisible();
    drawScreen();
}

// ============================================================
// 描画
// ============================================================

// 24文字で打ち切り・左寄せで描画（共同開発時に確認した提案・8/13深夜）
// 固定幅フォント（AsciiFont8x16）なら24文字x8px=192px<240px——左寄せでも右端見切れなし。
// エディタのソースはコードなので左寄せが自然（中央寄せは可変幅時代の名残り）
static void drawStringFit(const char* s, int x, int y)
{
    char tmp[25];
    int len = 0;
    while (s[len] != '\0' && len < kCols) {
        tmp[len] = s[len];
        len++;
    }
    tmp[len] = '\0';
    lcd.drawString(tmp, x, y);  // 左寄せ
}

// 1行目: モード表示
static void drawStatus()
{
    char line[40];
    switch (g_mode) {
        case Mode::PRO:
            snprintf(line, sizeof(line), "PRO> CASL2 line %d/%d",
                     bufRowOf(g_cursor) + 1, bufLineCount());
            break;
        case Mode::ASM:
            snprintf(line, sizeof(line), "ASM> assemble");
            break;
        case Mode::RUN:
            snprintf(line, sizeof(line), "RUN> output");
            break;
    }
    drawStringFit(line, 0, 0);
}

// 2〜14行目: テキストウィンドウ
static void drawText()
{
    if (g_mode == Mode::RUN) {
        // 実行結果（OUT出力）を表示
        int line = 0, pos = 0;
        while (pos <= g_outLen && line < kWinRows) {
            char outLine[kCols + 1];
            int len = 0;
            while (pos < g_outLen && g_outBuf[pos] != '\n' && len < kCols) {
                outLine[len++] = g_outBuf[pos++];
            }
            outLine[len] = '\0';
            drawStringFit(outLine, 0, (1 + line) * kCharH);
            line++;
            if (pos < g_outLen && g_outBuf[pos] == '\n') pos++;
        }
    } else {
        // ソース表示（スクロール対応）
        for (int i = 0; i < kWinRows; i++) {
            char line[kCols + 1];
            getLineText(g_scroll + i, line, sizeof(line));
            drawStringFit(line, 0, (1 + i) * kCharH);
        }
    }
}

// カーソル（反転ブロック）
static void drawCursor()
{
    if (g_mode != Mode::PRO) return;
    int row = bufRowOf(g_cursor) - g_scroll;
    int col = bufColOf(g_cursor);
    if (row < 0 || row >= kWinRows || col >= kCols) return;
    int x = col * kCharW;
    int y = (1 + row) * kCharH;
    lcd.fillRect(x, y, kCharW, kCharH, 0xFFFF);  // 白背景
    char c = (g_cursor < g_bufLen && g_buf[g_cursor] != '\n') ? g_buf[g_cursor] : ' ';
    lcd.drawChar(c, x, y, 0x0000);     // 黒文字
}

// 6行目: メッセージ
static void drawMsg()
{
    drawStringFit(g_msg, 0, kMsgRow * kCharH);
}

// 7行目: モード切替ヒントバー（全モードで常時表示・共同開発時に確認した提案）
// 7行x16px=112px<135px——G850の6行+ヒント1行が収まる
static void drawHint()
{
    drawStringFit("FN1:PRO FN2:ASM FN3:RUN", 0, 6 * kCharH);
}

static void drawScreen()
{
    lcd.fillScreen(0x0000);
    drawStatus();
    drawText();
    drawCursor();
    drawMsg();
    drawHint();
}

// ============================================================
// 実行（ASM / RUN）
// ============================================================

// ASMモード: アセンブル実行
static void runAssemble()
{
    std::string src(g_buf, g_bufLen);
    g_asm = casl::assemble(src);
    if (g_asm.ok) {
        snprintf(g_msg, sizeof(g_msg), "OK: %d words. FN>RUN", g_asm.obj_words);
    } else {
        snprintf(g_msg, sizeof(g_msg), "ERR: %s", g_asm.error.c_str());
    }
}

// OUT出力の受取先（RUNモード・実行中はバッファにためる）
static void runOutput(char ch)
{
    if (g_outLen < (int)sizeof(g_outBuf) - 1) {
        g_outBuf[g_outLen++] = ch;
    }
}

// RUNモード: COMET VM実行
static void runProgram()
{
    if (!g_asm.ok) {
        snprintf(g_msg, sizeof(g_msg), "Assemble first! FN>PRO");
        return;
    }

    comet::Comet* c = new comet::Comet();
    comet::reset(*c);
    for (int i = 0; i < g_asm.obj_words; i++) {
        c->writeWord(comet::OBJ_BASE + i, g_asm.obj[i]);
    }

    // OUT出力をバッファにためる
    g_outLen = 0;
    c->out_func = runOutput;

    // 実行（PCがオブジェクト領域外に出るか、ステップ上限で終了）
    uint16_t endAddr = comet::OBJ_BASE + g_asm.obj_words;
    int steps = 0;
    const int kMaxSteps = 10000;
    while (steps < kMaxSteps) {
        comet::step(*c);
        steps++;
        if (c->PC < comet::OBJ_BASE || c->PC >= endAddr) break;
    }

    if (steps >= kMaxSteps) {
        snprintf(g_msg, sizeof(g_msg), "STOP: step limit GR1=%d", c->GR[1]);
    } else {
        snprintf(g_msg, sizeof(g_msg), "RUN OK (%d steps) GR1=%d", steps, c->GR[1]);
    }
    delete c;
}

// FNキー（MODEキー相当）でモード遷移
static void nextMode()
{
    switch (g_mode) {
        case Mode::PRO:
            g_mode = Mode::ASM;
            runAssemble();
            break;
        case Mode::ASM:
            g_mode = Mode::RUN;
            runProgram();
            break;
        case Mode::RUN:
            g_mode = Mode::PRO;
            snprintf(g_msg, sizeof(g_msg), "Ready. FN=MODE");
            break;
    }
    drawScreen();
}

// ============================================================
// キー処理
// ============================================================

// ENTER: 改行 + 自動インデント（前の行の行頭スペースを引き継ぐ）
static void handleEnter()
{
    // 現在の行のインデント（行頭スペース数）を数える
    int start = lineStartOf(g_cursor);
    int indent = 0;
    while (start + indent < g_bufLen && g_buf[start + indent] == ' ') {
        indent++;
    }
    bufInsert('\n');
    for (int i = 0; i < indent; i++) {
        bufInsert(' ');
    }
}

// PROモード（エディタ）のキー処理
static void handleProKey(uint8_t ch)
{
    if (ch == 0x28) {  // ENTER（自動インデント付き）
        handleEnter();
    } else if (ch == 0x2A) {  // BS
        bufBackspace();
    } else if (ch == 0x2B) {  // TAB
        for (int i = 0; i < 4; i++) bufInsert(' ');
    } else if (ch >= 0x20 && ch < 0x7F) {  // 表示可能文字
        bufInsert(ch);
    } else {
        return;
    }
    ensureCursorVisible();
    drawScreen();
}

// ============================================================
// setup / loop
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("=== COMET VM on Cardputer (2-3) ===");

    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(200);
    lcd.fillScreen(0x0000);
    lcd.setTextColor(0xFFFF, 0x0000);
    lcd.setFont(&fonts::AsciiFont8x16);  // 固定幅フォント（G850の24文字x6行を正確に再現）

    if (!kbd.begin()) {
        Serial.println("Keyboard NG (TCA8418 not found)");
        snprintf(g_msg, sizeof(g_msg), "Keyboard NG");
    } else {
        Serial.println("Keyboard OK");
    }

    // 初期デモソース（加算テスト）をバッファに
    const char* demo =
        "START\n"
        "     LD   GR1, X\n"
        "     ADDA GR1, Y\n"
        "     RET\n"
        "X    DC   5\n"
        "Y    DC   3\n"
        "END\n";
    g_bufLen = 0;
    g_cursor = 0;
    const char* p = demo;
    while (*p) {
        bufInsert(*p++);
    }
    g_cursor = 0;

    drawScreen();
}

void loop()
{
    uint8_t ch;
    if (kbd.readChar(ch)) {
        if (kbd.isFnHeld()) {
            // FN+キーの組み合わせ（FN単独の0xFFは何もしない）
            switch (ch) {
                case '1':  // FN+1: PROモードへ直接
                    g_mode = Mode::PRO;
                    snprintf(g_msg, sizeof(g_msg), "Mode: PRO (edit)");
                    drawScreen();
                    break;
                case '2':  // FN+2: ASMモードへ直接
                    g_mode = Mode::ASM;
                    runAssemble();
                    drawScreen();
                    break;
                case '3':  // FN+3: RUNモードへ直接
                    g_mode = Mode::RUN;
                    runProgram();
                    drawScreen();
                    break;
                case ';':  // FN+;: ↑（Cardputerのキーボード刻印に合わせる）
                    moveCursorUp();
                    break;
                case ',':  // FN+,: ←
                    moveCursorLeft();
                    break;
                case '.':  // FN+.: ↓
                    moveCursorDown();
                    break;
                case '/':  // FN+/: →
                    moveCursorRight();
                    break;
                default:
                    break;
            }
        } else if (ch == 0xFF) {
            // FN単独押下: モード切替はFN+1/2/3に変更（FNは十字キーに解放）
            // 何もしない
        } else {
            switch (g_mode) {
                case Mode::PRO:
                    handleProKey(ch);
                    break;
                case Mode::ASM:
                case Mode::RUN:
                    // 実行結果表示中はFN+数字のみ受付
                    break;
            }
        }
        Serial.printf("Key: 0x%02X mode=%d\n", ch, (int)g_mode);
    }
    delay(5);
}
