#ifndef RAYLIB_LIBRETRO_CONFIG_H
#define RAYLIB_LIBRETRO_CONFIG_H

#include <stdbool.h>

#define LIBRETRO_CONFIG_PATH_MAX 512
#define LIBRETRO_CONFIG_FILENAME "raylib-libretro-config.txt"

typedef struct LibretroConfig {
    char playlistsDir[LIBRETRO_CONFIG_PATH_MAX];
} LibretroConfig;

#if defined(__cplusplus)
extern "C" {
#endif

void LoadLibretroConfig(LibretroConfig* out);
bool SaveLibretroConfig(const LibretroConfig* config);

#if defined(__cplusplus)
}
#endif

#endif

#ifdef RAYLIB_LIBRETRO_CONFIG_IMPLEMENTATION
#ifndef RAYLIB_LIBRETRO_CONFIG_IMPLEMENTATION_ONCE
#define RAYLIB_LIBRETRO_CONFIG_IMPLEMENTATION_ONCE

#include <string.h>
#include "raylib.h"

#if defined(__cplusplus)
extern "C" {
#endif

void LoadLibretroConfig(LibretroConfig* out) {
    out->playlistsDir[0] = '\0';
    if (!FileExists(LIBRETRO_CONFIG_FILENAME)) {
        return;
    }
    int size = 0;
    unsigned char* data = LoadFileData(LIBRETRO_CONFIG_FILENAME, &size);
    if (data == NULL) {
        return;
    }
    int copy = size;
    if (copy >= LIBRETRO_CONFIG_PATH_MAX) {
        copy = LIBRETRO_CONFIG_PATH_MAX - 1;
    }
    memcpy(out->playlistsDir, data, (size_t)copy);
    out->playlistsDir[copy] = '\0';
    // Strip trailing CR/LF/space.
    for (int i = copy - 1; i >= 0; i--) {
        char c = out->playlistsDir[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            out->playlistsDir[i] = '\0';
        } else {
            break;
        }
    }
    UnloadFileData(data);
}

bool SaveLibretroConfig(const LibretroConfig* config) {
    return SaveFileText(LIBRETRO_CONFIG_FILENAME, (char*)config->playlistsDir);
}

#if defined(__cplusplus)
}
#endif

#endif
#endif
