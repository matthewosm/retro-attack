/**********************************************************************************************
*
*   raylib-libretro - A libretro frontend using raylib.
*
*   LICENSE: zlib/libpng
*
*   raylib-libretro is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2020 Rob Loach (@RobLoach)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "raylib.h"

#define RAYLIB_APP_IMPLEMENTATION
#include "raylib-app.h"

#define RAYLIB_LIBRETRO_IMPLEMENTATION
#include "raylib-libretro.h"

#define RAYLIB_LIBRETRO_SHADERS_IMPLEMENTATION
#include "../include/raylib-libretro-shaders.h"

#define RAYLIB_LIBRETRO_MENU_IMPLEMENTATION
#include "../include/raylib-libretro-menu.h"

#define MATCH_ROUNDS           10
#define COIN_TYPES             3
#define COUNTDOWN_FADE_SECONDS 0.5
#define COUNTDOWN_SECONDS      (COUNTDOWN_FADE_SECONDS + 4.0)
#define ROULETTE_GAME_SECONDS  10.0
#define FINAL_SECONDS          5.0
#define ROULETTE_MAX_RETRIES   5

typedef enum { COIN_COPPER = 0, COIN_SILVER = 1, COIN_GOLD = 2 } CoinType;

static const int   COIN_VALUES [COIN_TYPES] = { 1, 3, 5 };
static const int   COIN_INITIAL[COIN_TYPES] = { 5, 3, 2 };
static const Color COIN_COLORS [COIN_TYPES] = {
    { 184, 115,  51, 255 },
    { 205, 205, 215, 255 },
    { 255, 200,  40, 255 },
};

typedef enum {
    ROULETTE_BETWEEN,
    ROULETTE_PLAYING,
    ROULETTE_FINAL,
} RouletteState;

typedef struct {
    LibretroMenu* menu;
    bool rouletteActive;
    RouletteState state;

    int  round;
    int  score[2];
    int  coins[2][COIN_TYPES];
    int  cursor[2];
    bool locked[2];
    int  bet[2];

    int    nextGameIdx;
    double countdownEnd;
    double gameEnd;
    double finalEnd;
    bool   attackDrawn;

    Font retroFont;
    Font attackFont;
    bool retroFontLoaded;
    bool attackFontLoaded;

    Texture2D coinTex[COIN_TYPES];
    bool coinTexLoaded[COIN_TYPES];
} AppData;

static int PickRouletteCandidate(LibretroPlaylistLibrary* lib) {
    int available = 0;
    for (int i = 0; i < lib->count; i++) {
        if (!lib->entries[i].blacklisted) available++;
    }
    if (available <= 0) return -1;

    int target = GetRandomValue(0, available - 1);
    for (int i = 0; i < lib->count; i++) {
        if (lib->entries[i].blacklisted) continue;
        if (target == 0) return i;
        target--;
    }
    return -1;
}

static bool LoadRouletteGameByIndex(AppData* data, int idx) {
    LibretroPlaylistLibrary* lib = &data->menu->library;
    if (lib->count <= 0) {
        data->rouletteActive = false;
        return false;
    }

    if (IsLibretroReady()) {
        UnloadLibretroGame();
        CloseLibretro();
    }

    for (int i = 0; i < ROULETTE_MAX_RETRIES; i++) {
        if (idx < 0 || idx >= lib->count || lib->entries[idx].blacklisted) {
            idx = PickRouletteCandidate(lib);
        }
        if (idx < 0) {
            TraceLog(LOG_WARNING, "ROULETTE: all entries blacklisted, stopping");
            data->rouletteActive = false;
            return false;
        }
        LibretroPlaylistEntry* pick = &lib->entries[idx];
        TraceLog(LOG_INFO, "ROULETTE: %s (core: %s)", pick->label, pick->corePath);

        if (!InitLibretro(pick->corePath)) {
            TraceLog(LOG_ERROR, "ROULETTE: InitLibretro failed, blacklisting %s", pick->path);
            pick->blacklisted = true;
            idx = -1;
            continue;
        }
        if (!LoadLibretroGame(pick->path)) {
            TraceLog(LOG_ERROR, "ROULETTE: LoadLibretroGame failed, blacklisting %s", pick->path);
            pick->blacklisted = true;
            CloseLibretro();
            idx = -1;
            continue;
        }
        return true;
    }

    TraceLog(LOG_WARNING, "ROULETTE: gave up after %d failed attempts", ROULETTE_MAX_RETRIES);
    return false;
}

static void DrawTextStroked(Font font, const char* text, Vector2 pos, float size, float spacing, Color fill, Color stroke, float strokeWidth) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            Vector2 o = { pos.x + dx * strokeWidth, pos.y + dy * strokeWidth };
            DrawTextEx(font, text, o, size, spacing, stroke);
        }
    }
    DrawTextEx(font, text, pos, size, spacing, fill);
}

static void DrawScoreGradient(Font font, const char* text, Vector2 pos, float size, float spacing) {
    Vector2 m = MeasureTextEx(font, text, size, spacing);

    // Hard drop shadow like the ATTACK style.
    Vector2 shadow = { pos.x + size * 0.04f, pos.y + size * 0.04f };
    DrawTextEx(font, text, shadow, size, spacing, BLACK);

    // Banded gradient: 8 stops, pale yellow -> deep red.
    Color stops[] = {
        (Color){ 255, 250, 180, 255 },
        (Color){ 255, 240, 110, 255 },
        (Color){ 255, 220,  60, 255 },
        (Color){ 255, 185,   0, 255 },
        (Color){ 255, 140,   0, 255 },
        (Color){ 255,  85,   0, 255 },
        (Color){ 220,  40,   0, 255 },
        (Color){ 160,  10,   0, 255 },
    };
    int bands = sizeof(stops) / sizeof(stops[0]);
    float bandH = m.y / (float)bands;
    for (int b = 0; b < bands; b++) {
        int y0 = (int)(pos.y + b * bandH);
        int h  = (int)(bandH) + 2;
        BeginScissorMode((int)pos.x - 16, y0, (int)m.x + 32, h);
        DrawTextEx(font, text, pos, size, spacing, stops[b]);
        EndScissorMode();
    }
}

static void DrawCenteredText(Font font, const char* text, float cy, float size, Color color) {
    Vector2 m = MeasureTextEx(font, text, size, size * 0.06f);
    Vector2 pos = { (GetScreenWidth() - m.x) * 0.5f, cy - m.y * 0.5f };
    DrawTextEx(font, text, pos, size, size * 0.06f, color);
}

static void DrawCenteredStroked(Font font, const char* text, float cy, float size, Color fill, Color stroke) {
    Vector2 m = MeasureTextEx(font, text, size, size * 0.06f);
    Vector2 pos = { (GetScreenWidth() - m.x) * 0.5f, cy - m.y * 0.5f };
    DrawTextStroked(font, text, pos, size, size * 0.06f, fill, stroke, size * 0.05f);
}

static int TotalCoins(AppData* data, int p) {
    int n = 0;
    for (int t = 0; t < COIN_TYPES; t++) n += data->coins[p][t];
    return n;
}

static void DrawCoinBank(AppData* data, int p, float originX, float originY, float cellW) {
    float r = cellW * 0.45f;
    for (int t = 0; t < COIN_TYPES; t++) {
        float rowY = originY + t * (cellW * 1.05f);
        int capacity = COIN_INITIAL[t];
        int remaining = data->coins[p][t];
        for (int i = 0; i < capacity; i++) {
            float cx = originX + (i + 0.5f) * cellW;
            float cy = rowY + cellW * 0.5f;
            bool faded = (i >= remaining);
            Color tint = faded ? (Color){ 255, 255, 255, 80 } : WHITE;

            if (data->coinTexLoaded[t]) {
                Texture2D tex = data->coinTex[t];
                Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
                float size = r * 2.0f;
                Rectangle dst = { cx - size * 0.5f, cy - size * 0.5f, size, size };
                DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0, tint);
            } else {
                Color col = COIN_COLORS[t];
                if (faded) col.a = 80;
                DrawCircle((int)cx, (int)cy, r, col);
                DrawCircleLines((int)cx, (int)cy, r, (Color){ 0, 0, 0, 220 });
            }

            const char* txt = TextFormat("%d", COIN_VALUES[t]);
            float tsz = r * 1.05f;
            Vector2 tm = MeasureTextEx(data->retroFont, txt, tsz, tsz * 0.05f);
            Color fg = faded ? (Color){ 0, 0, 0, 140 } : BLACK;
            Color bg = faded ? (Color){ 255, 255, 255, 80 } : WHITE;
            DrawTextStroked(data->retroFont, txt,
                            (Vector2){ cx - tm.x * 0.5f, cy - tm.y * 0.5f },
                            tsz, tsz * 0.05f, bg, fg, tsz * 0.08f);
        }

    }

    // Arrow cursor pointing at the currently selected row.
    int activeT = data->locked[p] ? data->bet[p] : data->cursor[p];
    float rowY  = originY + activeT * (cellW * 1.05f);
    float cy    = rowY + cellW * 0.5f;
    float ax    = originX - cellW * 0.35f;
    float size  = cellW * 0.35f;
    Color arrowCol = data->locked[p] ? (Color){ 255, 215, 0, 255 } : YELLOW;
    DrawTriangle(
        (Vector2){ ax,        cy - size * 0.5f },
        (Vector2){ ax,        cy + size * 0.5f },
        (Vector2){ ax + size, cy               },
        arrowCol);

    if (data->locked[p]) {
        const char* lockTxt = "LOCKED";
        float lsz = cellW * 0.5f;
        Vector2 lm = MeasureTextEx(data->retroFont, lockTxt, lsz, lsz * 0.06f);
        float lockX = originX + (COIN_INITIAL[0] * cellW) * 0.5f - lm.x * 0.5f;
        float lockY = originY - lm.y - 4;
        DrawTextStroked(data->retroFont, lockTxt, (Vector2){ lockX, lockY }, lsz, lsz * 0.06f, (Color){ 255, 215, 0, 255 }, BLACK, lsz * 0.06f);
    }
}

static void DrawRouletteBetweenScreen(AppData* data) {
    ClearBackground(BLACK);

    int w = GetScreenWidth();
    int h = GetScreenHeight();
    float marginY = h * 0.05f;
    float marginX = w * 0.04f;
    float top = marginY;
    float bottom = h - marginY;
    float contentH = bottom - top;

    // ROUND N header.
    {
        const char* roundTxt = TextFormat("ROUND %d", data->round);
        float sz = h * 0.09f;
        DrawCenteredStroked(data->retroFont, roundTxt, top + contentH * 0.05f, sz, WHITE, BLACK);
    }

    // 1P / 2P labels and scores.
    const char* labels[2] = { "1P", "2P" };
    for (int p = 0; p < 2; p++) {
        float cx = w * (0.27f + 0.46f * p);

        float labelSize = h * 0.07f;
        float labelSpacing = labelSize * 0.08f;
        Vector2 lm = MeasureTextEx(data->retroFont, labels[p], labelSize, labelSpacing);
        Vector2 lpos = { cx - lm.x * 0.5f, top + contentH * 0.14f };
        DrawTextStroked(data->retroFont, labels[p], lpos, labelSize, labelSpacing, WHITE, BLACK, labelSize * 0.06f);

        const char* s = TextFormat("%d", data->score[p]);
        float scoreSize = h * 0.15f;
        float scoreSpacing = scoreSize * 0.06f;
        Vector2 sm = MeasureTextEx(data->retroFont, s, scoreSize, scoreSpacing);
        Vector2 spos = { cx - sm.x * 0.5f, top + contentH * 0.24f };
        DrawTextStroked(data->retroFont, s, spos, scoreSize, scoreSpacing, WHITE, BLACK, scoreSize * 0.06f);
    }

    // Divider.
    DrawLine(w / 2, (int)(top + contentH * 0.10f), w / 2, (int)(top + contentH * 0.52f), (Color){ 255, 255, 255, 60 });

    // NEXT GAME label + title.
    {
        DrawCenteredStroked(data->retroFont, "NEXT GAME", top + contentH * 0.57f, h * 0.045f, (Color){ 180, 220, 255, 255 }, BLACK);

        const char* title = "???";
        LibretroPlaylistLibrary* lib = &data->menu->library;
        if (data->nextGameIdx >= 0 && data->nextGameIdx < lib->count) {
            title = lib->entries[data->nextGameIdx].label;
        }
        DrawCenteredStroked(data->retroFont, title, top + contentH * 0.63f, h * 0.06f, WHITE, BLACK);
    }

    // Coin banks (left = 1P, right = 2P). Bounded horizontally by marginX.
    float maxBankWidth = (w * 0.5f - marginX - w * 0.03f);
    float cellW = fminf(maxBankWidth / (float)COIN_INITIAL[0], h * 0.075f);
    float banksY = top + contentH * 0.72f;
    float bankWidth = COIN_INITIAL[0] * cellW;
    float leftX  = marginX + w * 0.02f;
    float rightX = w - marginX - w * 0.02f - bankWidth;
    DrawCoinBank(data, 0, leftX,  banksY, cellW);
    DrawCoinBank(data, 1, rightX, banksY, cellW);

    // Help text.
    if (!data->locked[0] || !data->locked[1]) {
        DrawCenteredText(data->retroFont, "1P: A / D / W          2P:  \xE2\x86\x90  /  \xE2\x86\x92  /  \xE2\x86\x91",
                         bottom - h * 0.025f, h * 0.028f, (Color){ 200, 200, 200, 200 });
    }

    // Countdown overlay: fade the between UI to black, then show 3, 2, 1, ATTACK.
    if (data->countdownEnd > 0.0) {
        double now = GetTime();
        double elapsed = COUNTDOWN_SECONDS - (data->countdownEnd - now);
        if (elapsed < 0) elapsed = 0;

        float fade = (float)(elapsed / COUNTDOWN_FADE_SECONDS);
        if (fade > 1.0f) fade = 1.0f;
        DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, (unsigned char)(fade * 255) });

        // Beat index starts after the fade completes. 0 -> "3", 1 -> "2", 2 -> "1", 3 -> "ATTACK".
        int beat = (int)(elapsed - COUNTDOWN_FADE_SECONDS);
        if (beat < 0) beat = 0;
        if (beat > 3) beat = 3;

        if (fade >= 1.0f) {
            const char* txt;
            float sz;
            if (beat < 3) { txt = TextFormat("%d", 3 - beat); sz = h * 0.55f; }
            else          { txt = "ATTACK";                    sz = h * 0.40f; }
            float sp = sz * 0.02f;
            Vector2 m = MeasureTextEx(data->attackFont, txt, sz, sp);
            Vector2 pos = { (w - m.x) * 0.5f, (h - m.y) * 0.5f };
            DrawScoreGradient(data->attackFont, txt, pos, sz, sp);
            if (beat >= 3) data->attackDrawn = true;
        }
    }
}

static void DrawRouletteFinalScreen(AppData* data) {
    ClearBackground(BLACK);

    int h = GetScreenHeight();

    DrawCenteredStroked(data->retroFont, "GAME OVER", h * 0.15f, h * 0.10f, WHITE, BLACK);

    const char* winTxt;
    if      (data->score[0] >  data->score[1]) winTxt = "1P WINS";
    else if (data->score[1] >  data->score[0]) winTxt = "2P WINS";
    else                                       winTxt = "DRAW";

    {
        float sz = h * 0.22f;
        float sp = sz * 0.02f;
        Vector2 m = MeasureTextEx(data->attackFont, winTxt, sz, sp);
        Vector2 pos = { (GetScreenWidth() - m.x) * 0.5f, h * 0.34f };
        DrawScoreGradient(data->attackFont, winTxt, pos, sz, sp);
    }

    const char* scores = TextFormat("%d   -   %d", data->score[0], data->score[1]);
    DrawCenteredStroked(data->retroFont, scores, h * 0.72f, h * 0.10f, WHITE, BLACK);

    DrawCenteredText(data->retroFont, "returning to menu...", h * 0.90f, h * 0.035f, (Color){ 200, 200, 200, 200 });
}

static void ResetMatch(AppData* data) {
    data->round = 1;
    data->score[0] = data->score[1] = 0;
    for (int p = 0; p < 2; p++) {
        for (int t = 0; t < COIN_TYPES; t++) data->coins[p][t] = COIN_INITIAL[t];
        data->cursor[p] = 0;
        data->locked[p] = false;
        data->bet[p] = 0;
    }
    data->countdownEnd = 0.0;
    data->gameEnd = 0.0;
    data->finalEnd = 0.0;
    data->state = ROULETTE_BETWEEN;
    data->nextGameIdx = PickRouletteCandidate(&data->menu->library);
}

static void AdvanceCursor(AppData* data, int p, int dir) {
    if (data->locked[p]) return;
    int t = data->cursor[p];
    for (int i = 0; i < COIN_TYPES; i++) {
        t = (t + dir + COIN_TYPES) % COIN_TYPES;
        if (data->coins[p][t] > 0) { data->cursor[p] = t; return; }
    }
}

static void LockBet(AppData* data, int p) {
    if (data->locked[p]) return;
    if (data->coins[p][data->cursor[p]] <= 0) return;
    data->bet[p] = data->cursor[p];
    data->locked[p] = true;
}

static void HandleBettingInput(AppData* data) {
    if (data->countdownEnd > 0.0) return;
    if (IsKeyPressed(KEY_A))     AdvanceCursor(data, 0, -1);
    if (IsKeyPressed(KEY_D))     AdvanceCursor(data, 0, +1);
    if (IsKeyPressed(KEY_W))     LockBet(data, 0);
    if (IsKeyPressed(KEY_LEFT))  AdvanceCursor(data, 1, -1);
    if (IsKeyPressed(KEY_RIGHT)) AdvanceCursor(data, 1, +1);
    if (IsKeyPressed(KEY_UP))    LockBet(data, 1);
}

bool Init(void** userData, int argc, char** argv) {
    SetWindowMinSize(400, 300);

    AppData* data = (AppData*)MemAlloc(sizeof(AppData));
    *userData = data;

    InitAudioDevice();

    // Load score screen fonts. Falls back to default font if files are missing.
    // Codepoints: printable ASCII (32..126) + left/up/right arrows used in the help text.
    int fontCodepoints[95 + 3];
    for (int i = 0; i < 95; i++) fontCodepoints[i] = 32 + i;
    fontCodepoints[95] = 0x2190; // ←
    fontCodepoints[96] = 0x2191; // ↑
    fontCodepoints[97] = 0x2192; // →
    int fontCodepointCount = 95 + 3;

    if (FileExists("resources/fonts/BitcountGridDouble-Regular.ttf")) {
        data->retroFont = LoadFontEx("resources/fonts/BitcountGridDouble-Regular.ttf", 96, fontCodepoints, fontCodepointCount);
        SetTextureFilter(data->retroFont.texture, TEXTURE_FILTER_BILINEAR);
        data->retroFontLoaded = true;
    } else {
        TraceLog(LOG_WARNING, "SCORE: BitcountGridDouble-Regular.ttf missing, using default font");
        data->retroFont = GetFontDefault();
        data->retroFontLoaded = false;
    }
    if (FileExists("resources/fonts/PermanentMarker-Regular.ttf")) {
        data->attackFont = LoadFontEx("resources/fonts/PermanentMarker-Regular.ttf", 96, fontCodepoints, fontCodepointCount);
        SetTextureFilter(data->attackFont.texture, TEXTURE_FILTER_BILINEAR);
        data->attackFontLoaded = true;
    } else {
        TraceLog(LOG_WARNING, "SCORE: PermanentMarker-Regular.ttf missing, using default font");
        data->attackFont = GetFontDefault();
        data->attackFontLoaded = false;
    }

    const char* coinPaths[COIN_TYPES] = {
        "resources/coins/copper.png",
        "resources/coins/silver.png",
        "resources/coins/gold.png",
    };
    for (int t = 0; t < COIN_TYPES; t++) {
        if (FileExists(coinPaths[t])) {
            data->coinTex[t] = LoadTexture(coinPaths[t]);
            SetTextureFilter(data->coinTex[t], TEXTURE_FILTER_BILINEAR);
            data->coinTexLoaded[t] = true;
        } else {
            TraceLog(LOG_WARNING, "COIN: %s missing", coinPaths[t]);
            data->coinTexLoaded[t] = false;
        }
    }

    // Load the shaders and the menu.
    LoadLibretroShaders();
    data->menu = InitLibretroMenu();
    if (!data->menu) {
        TraceLog(LOG_ERROR, "Failed to initialize menu");
        UnloadLibretroShaders();
        CloseAudioDevice();
        return false;
    }

    // Parse the command line arguments.
    if (argc > 1) {
        // Initialize the given core.
        if (InitLibretro(argv[1])) {
            // Load the given game.
            const char* gameFile = (argc > 2) ? argv[2] : NULL;
            if (LoadLibretroGame(gameFile)) {
                data->menu->active = false;
            }
        }
    }

    return true;
}

bool UpdateDrawFrame(void* userData) {
    AppData* data = (AppData*)userData;

    // Update the shaders.
    UpdateLibretroShaders(GetFrameTime());

    // Start roulette when the menu button was clicked last frame.
    if (data->menu->rouletteRequested) {
        data->menu->rouletteRequested = false;
        if (data->menu->library.count > 0) {
            data->rouletteActive = true;
            ResetMatch(data);
        } else {
            TraceLog(LOG_WARNING, "ROULETTE: no games in library — set Playlists Folder in Settings first");
        }
    }

    // Opening the menu while roulette is active cancels the match.
    if (data->rouletteActive && data->menu->active) {
        data->rouletteActive = false;
        if (IsLibretroReady()) { UnloadLibretroGame(); CloseLibretro(); }
    }

    if (data->rouletteActive) {
        if (data->state == ROULETTE_BETWEEN) {
            HandleBettingInput(data);

            if (data->countdownEnd == 0.0 && data->locked[0] && data->locked[1]) {
                data->countdownEnd = GetTime() + COUNTDOWN_SECONDS;
                data->attackDrawn = false;
            }

            if (data->countdownEnd > 0.0) {
                double elapsed = COUNTDOWN_SECONDS - (data->countdownEnd - GetTime());
                int beat = (int)(elapsed - COUNTDOWN_FADE_SECONDS);

                // Kick off the game load as soon as ATTACK has been drawn for one
                // frame — the blocking load then happens while ATTACK is on screen,
                // making the following game appear to load faster.
                if (beat >= 3 && data->attackDrawn) {
                    int winner = GetRandomValue(0, 1);
                    int loser  = 1 - winner;
                    data->score[winner] += COIN_VALUES[data->bet[winner]];
                    data->coins[winner][data->bet[winner]]--;
                    data->coins[loser ][data->bet[loser ]]--;
                    TraceLog(LOG_INFO, "ROUND %d: winner=%dP bet=%d loser=%dP bet=%d",
                             data->round, winner + 1, data->bet[winner], loser + 1, data->bet[loser]);

                    if (LoadRouletteGameByIndex(data, data->nextGameIdx)) {
                        data->state = ROULETTE_PLAYING;
                        data->gameEnd = GetTime() + ROULETTE_GAME_SECONDS;
                    } else {
                        data->rouletteActive = false;
                        data->menu->active = true;
                    }
                    data->attackDrawn = false;
                }
            }
        }
        else if (data->state == ROULETTE_PLAYING) {
            if (GetTime() >= data->gameEnd) {
                if (IsLibretroReady()) { UnloadLibretroGame(); CloseLibretro(); }
                data->round++;
                if (data->round > MATCH_ROUNDS) {
                    data->state = ROULETTE_FINAL;
                    data->finalEnd = GetTime() + FINAL_SECONDS;
                } else {
                    data->state = ROULETTE_BETWEEN;
                    data->countdownEnd = 0.0;
                    for (int p = 0; p < 2; p++) {
                        data->locked[p] = false;
                        data->cursor[p] = 0;
                        // Ensure cursor starts on a type that still has coins.
                        for (int t = 0; t < COIN_TYPES; t++) {
                            if (data->coins[p][t] > 0) { data->cursor[p] = t; break; }
                        }
                    }
                    data->nextGameIdx = PickRouletteCandidate(&data->menu->library);
                }
            }
        }
        else if (data->state == ROULETTE_FINAL) {
            if (GetTime() >= data->finalEnd) {
                data->rouletteActive = false;
                data->menu->active = true;
            }
        }
    }

    // Run a frame of the core (only while actually playing a round or normal usage).
    bool inRouletteUI = data->rouletteActive && data->state != ROULETTE_PLAYING;
    if (!data->menu->active && !inRouletteUI && IsLibretroReady()) {
        UpdateLibretro();
    }

    UpdateLibretroMenu();

    // Check if the core asks to be shutdown.
    if (LibretroShouldClose()) {
        UnloadLibretroGame();
        CloseLibretro();
    }

    // Render the libretro core.
    BeginDrawing();
    {
        ClearBackground(BLACK);

        if (data->rouletteActive && data->state == ROULETTE_BETWEEN) {
            DrawRouletteBetweenScreen(data);
        }
        else if (data->rouletteActive && data->state == ROULETTE_FINAL) {
            DrawRouletteFinalScreen(data);
        }
        else if (data->menu->active) {
            BeginLibretroShader();
            DrawLibretroTint(ColorAlpha(WHITE, 0.1f));
            EndShaderMode();
        }
        else {
            BeginLibretroShader();
            DrawLibretro();
            EndLibretroShader();
        }

        DrawLibretroMenu();
    }
    EndDrawing();

    // Fullscreen
    if (IsKeyReleased(KEY_F11)) {
        ToggleFullscreen();
    }

    // Screenshot
    else if (IsKeyReleased(KEY_F8)) {
        for (int i = 1; i < 1000; i++) {
            const char* screenshotName = TextFormat("screenshot-%i.png", i);
            if (!FileExists(screenshotName)) {
                TakeScreenshot(screenshotName);
                break;
            }
        }
    }

    // Save State
    else if (IsKeyReleased(KEY_F2)) {
        unsigned int size;
        void* saveData = GetLibretroSerializedData(&size);
        if (saveData != NULL) {
            SaveFileData(TextFormat("save_%s.sav", GetLibretroName()), saveData, (int)size);
            MemFree(saveData);
        }
    }

    // Load State
    else if (IsKeyReleased(KEY_F4)) {
        int dataSize;
        void* saveData = LoadFileData(TextFormat("save_%s.sav", GetLibretroName()), &dataSize);
        if (saveData != NULL) {
            SetLibretroSerializedData(saveData, (unsigned int)dataSize);
            MemFree(saveData);
        }
    }

    return true;
}

void Close(void* userData) {
    AppData* data = (AppData*)userData;

    // Unload the game and close the core.
    UnloadLibretroGame();
    CloseLibretro();

    CloseLibretroMenu();

    if (data->retroFontLoaded) UnloadFont(data->retroFont);
    if (data->attackFontLoaded) UnloadFont(data->attackFont);
    for (int t = 0; t < COIN_TYPES; t++) {
        if (data->coinTexLoaded[t]) UnloadTexture(data->coinTex[t]);
    }

    UnloadLibretroShaders();
    CloseAudioDevice();
    MemFree(data);
}

App Main() {
    return (App){
        .title = "raylib-libretro",
        .width = 800,
        .height = 600,
        .init = Init,
        .update = UpdateDrawFrame,
        .close = Close,
        .configFlags = FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT,
    };
}
