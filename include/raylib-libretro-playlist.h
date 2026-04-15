#ifndef RAYLIB_LIBRETRO_PLAYLIST_H
#define RAYLIB_LIBRETRO_PLAYLIST_H

#include <stdbool.h>
#include <stddef.h>

#define LIBRETRO_PLAYLIST_PATH_MAX 512
#define LIBRETRO_PLAYLIST_LABEL_MAX 256

typedef struct LibretroPlaylistEntry {
    char path[LIBRETRO_PLAYLIST_PATH_MAX];
    char label[LIBRETRO_PLAYLIST_LABEL_MAX];
    char corePath[LIBRETRO_PLAYLIST_PATH_MAX];
    bool blacklisted;
} LibretroPlaylistEntry;

typedef struct LibretroPlaylistLibrary {
    LibretroPlaylistEntry* entries;
    int count;
    int capacity;
} LibretroPlaylistLibrary;

#if defined(__cplusplus)
extern "C" {
#endif

bool LoadLibretroPlaylistLibrary(LibretroPlaylistLibrary* lib, const char* playlistsDir);
void FreeLibretroPlaylistLibrary(LibretroPlaylistLibrary* lib);

#if defined(__cplusplus)
}
#endif

#endif

#ifdef RAYLIB_LIBRETRO_PLAYLIST_IMPLEMENTATION
#ifndef RAYLIB_LIBRETRO_PLAYLIST_IMPLEMENTATION_ONCE
#define RAYLIB_LIBRETRO_PLAYLIST_IMPLEMENTATION_ONCE

#include <string.h>
#include "raylib.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Skip whitespace. Returns new position.
static size_t LibretroPlaylist_SkipWs(const char* s, size_t p, size_t n) {
    while (p < n) {
        char c = s[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            p++;
        } else {
            break;
        }
    }
    return p;
}

// Parse a JSON string starting at s[p] (which must be '"'). Writes decoded
// bytes to out (null-terminated, truncated to outSize). Returns the position
// just past the closing quote, or 0 on failure.
static size_t LibretroPlaylist_ParseString(const char* s, size_t p, size_t n,
                                           char* out, size_t outSize) {
    if (p >= n || s[p] != '"') {
        return 0;
    }
    p++;
    size_t w = 0;
    while (p < n) {
        char c = s[p++];
        if (c == '"') {
            if (out && outSize > 0) {
                if (w >= outSize) w = outSize - 1;
                out[w] = '\0';
            }
            return p;
        }
        if (c == '\\' && p < n) {
            char esc = s[p++];
            char decoded = 0;
            switch (esc) {
                case '"':  decoded = '"'; break;
                case '\\': decoded = '\\'; break;
                case '/':  decoded = '/'; break;
                case 'n':  decoded = '\n'; break;
                case 'r':  decoded = '\r'; break;
                case 't':  decoded = '\t'; break;
                case 'b':  decoded = '\b'; break;
                case 'f':  decoded = '\f'; break;
                case 'u':
                    // Skip 4 hex digits; encode ASCII only.
                    if (p + 4 <= n) {
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = s[p + i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        }
                        p += 4;
                        if (cp < 0x80) decoded = (char)cp;
                        else decoded = '?';
                    }
                    break;
                default:
                    decoded = esc;
                    break;
            }
            if (out && w + 1 < outSize) out[w++] = decoded;
            continue;
        }
        if (out && w + 1 < outSize) out[w++] = c;
    }
    return 0;
}

// Skip a JSON value starting at s[p] (after whitespace). Handles strings,
// numbers, booleans, null, objects, arrays. Returns position after value,
// or 0 on failure.
static size_t LibretroPlaylist_SkipValue(const char* s, size_t p, size_t n) {
    p = LibretroPlaylist_SkipWs(s, p, n);
    if (p >= n) return 0;
    char c = s[p];
    if (c == '"') {
        return LibretroPlaylist_ParseString(s, p, n, NULL, 0);
    }
    if (c == '{' || c == '[') {
        char open = c;
        char close = (c == '{') ? '}' : ']';
        int depth = 1;
        p++;
        while (p < n && depth > 0) {
            char d = s[p];
            if (d == '"') {
                size_t np = LibretroPlaylist_ParseString(s, p, n, NULL, 0);
                if (np == 0) return 0;
                p = np;
            } else if (d == open) {
                depth++;
                p++;
            } else if (d == close) {
                depth--;
                p++;
            } else {
                p++;
            }
        }
        return (depth == 0) ? p : 0;
    }
    // number / true / false / null: read until delimiter.
    while (p < n) {
        char d = s[p];
        if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t' ||
            d == '\r' || d == '\n') {
            break;
        }
        p++;
    }
    return p;
}

// Find the next object member key at the current position (inside an object,
// after '{' or after ','). On success returns position of the ':' + 1, and
// writes the key into keyOut. Returns 0 if no more members (found '}').
static size_t LibretroPlaylist_NextMember(const char* s, size_t p, size_t n,
                                          char* keyOut, size_t keyOutSize) {
    p = LibretroPlaylist_SkipWs(s, p, n);
    if (p >= n) return 0;
    if (s[p] == '}') return 0;
    if (s[p] == ',') {
        p++;
        p = LibretroPlaylist_SkipWs(s, p, n);
    }
    if (p >= n || s[p] != '"') return 0;
    size_t afterKey = LibretroPlaylist_ParseString(s, p, n, keyOut, keyOutSize);
    if (afterKey == 0) return 0;
    afterKey = LibretroPlaylist_SkipWs(s, afterKey, n);
    if (afterKey >= n || s[afterKey] != ':') return 0;
    return afterKey + 1;
}

static void LibretroPlaylist_PushEntry(LibretroPlaylistLibrary* lib,
                                       const LibretroPlaylistEntry* entry) {
    if (lib->count >= lib->capacity) {
        int newCap = lib->capacity == 0 ? 64 : lib->capacity * 2;
        LibretroPlaylistEntry* resized = (LibretroPlaylistEntry*)MemRealloc(
            lib->entries, (unsigned int)(newCap * (int)sizeof(LibretroPlaylistEntry)));
        if (resized == NULL) return;
        lib->entries = resized;
        lib->capacity = newCap;
    }
    lib->entries[lib->count++] = *entry;
}

static void LibretroPlaylist_ParseFile(LibretroPlaylistLibrary* lib,
                                       const char* text, size_t n) {
    char defaultCore[LIBRETRO_PLAYLIST_PATH_MAX] = {0};

    // Expect top-level object.
    size_t p = LibretroPlaylist_SkipWs(text, 0, n);
    if (p >= n || text[p] != '{') return;
    p++;

    char key[64];
    size_t itemsStart = 0;

    for (;;) {
        size_t afterColon = LibretroPlaylist_NextMember(text, p, n, key, sizeof(key));
        if (afterColon == 0) break;
        p = LibretroPlaylist_SkipWs(text, afterColon, n);
        if (strcmp(key, "default_core_path") == 0 && p < n && text[p] == '"') {
            p = LibretroPlaylist_ParseString(text, p, n, defaultCore, sizeof(defaultCore));
            if (p == 0) return;
        } else if (strcmp(key, "items") == 0 && p < n && text[p] == '[') {
            itemsStart = p;
            p = LibretroPlaylist_SkipValue(text, p, n);
            if (p == 0) return;
        } else {
            p = LibretroPlaylist_SkipValue(text, p, n);
            if (p == 0) return;
        }
    }

    if (itemsStart == 0) return;

    // Walk items array.
    p = itemsStart + 1; // past '['
    for (;;) {
        p = LibretroPlaylist_SkipWs(text, p, n);
        if (p >= n) return;
        if (text[p] == ']') return;
        if (text[p] == ',') { p++; continue; }
        if (text[p] != '{') return;
        p++;

        LibretroPlaylistEntry entry;
        memset(&entry, 0, sizeof(entry));

        for (;;) {
            size_t afterColon = LibretroPlaylist_NextMember(text, p, n, key, sizeof(key));
            if (afterColon == 0) break;
            size_t vp = LibretroPlaylist_SkipWs(text, afterColon, n);
            if (strcmp(key, "path") == 0 && vp < n && text[vp] == '"') {
                p = LibretroPlaylist_ParseString(text, vp, n, entry.path, sizeof(entry.path));
                if (p == 0) return;
            } else if (strcmp(key, "label") == 0 && vp < n && text[vp] == '"') {
                p = LibretroPlaylist_ParseString(text, vp, n, entry.label, sizeof(entry.label));
                if (p == 0) return;
            } else if (strcmp(key, "core_path") == 0 && vp < n && text[vp] == '"') {
                char tmp[LIBRETRO_PLAYLIST_PATH_MAX];
                p = LibretroPlaylist_ParseString(text, vp, n, tmp, sizeof(tmp));
                if (p == 0) return;
                // "DETECT" means fall through to playlist default.
                if (tmp[0] != '\0' && strcmp(tmp, "DETECT") != 0) {
                    memcpy(entry.corePath, tmp, sizeof(entry.corePath));
                }
            } else {
                p = LibretroPlaylist_SkipValue(text, vp, n);
                if (p == 0) return;
            }
        }
        // Skip closing '}'.
        p = LibretroPlaylist_SkipWs(text, p, n);
        if (p < n && text[p] == '}') p++;

        if (entry.corePath[0] == '\0') {
            memcpy(entry.corePath, defaultCore, sizeof(entry.corePath));
        }
        if (entry.path[0] != '\0' && entry.corePath[0] != '\0' && FileExists(entry.path)) {
            LibretroPlaylist_PushEntry(lib, &entry);
        }
    }
}

bool LoadLibretroPlaylistLibrary(LibretroPlaylistLibrary* lib, const char* playlistsDir) {
    FreeLibretroPlaylistLibrary(lib);
    if (playlistsDir == NULL || playlistsDir[0] == '\0') return false;
    if (!DirectoryExists(playlistsDir)) return false;

    FilePathList files = LoadDirectoryFilesEx(playlistsDir, ".lpl", true);
    for (unsigned int i = 0; i < files.count; i++) {
        const char* path = files.paths[i];
        int size = 0;
        unsigned char* data = LoadFileData(path, &size);
        if (data == NULL) continue;
        LibretroPlaylist_ParseFile(lib, (const char*)data, (size_t)size);
        UnloadFileData(data);
    }
    UnloadDirectoryFiles(files);

    TraceLog(LOG_INFO, "PLAYLIST: Loaded %d entries from %s", lib->count, playlistsDir);
    return lib->count > 0;
}

void FreeLibretroPlaylistLibrary(LibretroPlaylistLibrary* lib) {
    if (lib->entries != NULL) {
        MemFree(lib->entries);
    }
    lib->entries = NULL;
    lib->count = 0;
    lib->capacity = 0;
}

#if defined(__cplusplus)
}
#endif

#endif
#endif
