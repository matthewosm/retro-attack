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

typedef struct {
    LibretroMenu* menu;
    bool rouletteActive;
    double nextSwitchTime;
} AppData;

#define ROULETTE_INTERVAL_SECONDS 10.0
#define ROULETTE_MAX_RETRIES 5

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

static void LoadRandomRouletteGame(AppData* data) {
    LibretroPlaylistLibrary* lib = &data->menu->library;
    if (lib->count <= 0) {
        data->rouletteActive = false;
        return;
    }

    UnloadLibretroGame();
    CloseLibretro();

    for (int i = 0; i < ROULETTE_MAX_RETRIES; i++) {
        int idx = PickRouletteCandidate(lib);
        if (idx < 0) {
            TraceLog(LOG_WARNING, "ROULETTE: all entries blacklisted, stopping");
            data->rouletteActive = false;
            return;
        }
        LibretroPlaylistEntry* pick = &lib->entries[idx];
        TraceLog(LOG_INFO, "ROULETTE: %s (core: %s)", pick->label, pick->corePath);

        if (!InitLibretro(pick->corePath)) {
            TraceLog(LOG_ERROR, "ROULETTE: InitLibretro failed, blacklisting %s", pick->path);
            pick->blacklisted = true;
            continue;
        }
        if (!LoadLibretroGame(pick->path)) {
            TraceLog(LOG_ERROR, "ROULETTE: LoadLibretroGame failed, blacklisting %s", pick->path);
            pick->blacklisted = true;
            CloseLibretro();
            continue;
        }
        return;
    }

    TraceLog(LOG_WARNING, "ROULETTE: gave up after %d failed attempts", ROULETTE_MAX_RETRIES);
}

bool Init(void** userData, int argc, char** argv) {
    SetWindowMinSize(400, 300);

    AppData* data = (AppData*)MemAlloc(sizeof(AppData));
    *userData = data;

    InitAudioDevice();

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
            data->nextSwitchTime = 0.0;
        } else {
            TraceLog(LOG_WARNING, "ROULETTE: no games in library — set Playlists Folder in Settings first");
        }
    }

    // Opening the menu while roulette is active stops auto-switching.
    if (data->rouletteActive && data->menu->active) {
        data->rouletteActive = false;
    }

    if (data->rouletteActive && GetTime() >= data->nextSwitchTime) {
        data->nextSwitchTime = GetTime() + ROULETTE_INTERVAL_SECONDS;
        LoadRandomRouletteGame(data);
    }

    // Run a frame of the core.
    if (!data->menu->active && IsLibretroReady()) {
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

        if (data->menu->active) {
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
