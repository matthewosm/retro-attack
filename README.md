# raylib-libretro :space_invader: [![Tests](https://github.com/RobLoach/raylib-libretro/workflows/Tests/badge.svg)](https://github.com/RobLoach/raylib-libretro/actions)

[libretro](https://www.libretro.com/) frontend to play emulators, game engines and media players, using [raylib](https://www.raylib.com). The [raylib-libretro.h](include/raylib-libretro.h) raylib extension allows integrating any raylib application with the libretro API. *Still in early development.*

![Screenshot of raylib-libretro](src/screenshot.png)

## Usage

``` sh
raylib-libretro [core] [game]
```

Both arguments are optional. Launching with no arguments brings up the in-app menu where you can configure a RetroArch playlists folder and start **Retro Roulette** mode.

## Retro Roulette

Retro Roulette turns the frontend into a randomized arcade: every 10 seconds it unloads the current game and boots a new random ROM picked from your RetroArch playlists.

### Setup

1. In RetroArch, scan your ROM folders so each system gets a `.lpl` playlist (e.g. `SNES.lpl`, `GENESIS.lpl`). Each playlist must have a valid `default_core_path` — that's how the roulette knows which libretro core to load for every game in it.
2. Launch `raylib-libretro` with no arguments. Press `F1` (or the gamepad menu button) to open the menu if it isn't already showing.
3. Open **Settings → Playlists Folder** and select your RetroArch `playlists` directory. Typical locations:
   - **Steam RetroArch (Windows):** `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\playlists`
   - **Standalone RetroArch (Windows):** `%APPDATA%\RetroArch\playlists`
   - **Linux:** `~/.config/retroarch/playlists`
   - **macOS:** `~/Library/Application Support/RetroArch/playlists`
4. The picked path is saved to `raylib-libretro-config.txt` next to the executable. On every launch the configured folder is rescanned and ROMs whose files no longer exist are skipped automatically.
5. Back out to the main menu and click **Start Roulette**. The first random game loads instantly; each subsequent game loads after a 10-second timer. Open the menu (`F1`) at any point to stop the roulette and stay on the current game.

### Notes

- Library entries inherit the `default_core_path` from each `.lpl` (any per-entry `core_path` of `"DETECT"` falls through to the default). Make sure each playlist's default core points at a real `*_libretro` shared library.
- The roulette interval is currently fixed at 10 seconds (`ROULETTE_INTERVAL_SECONDS` in [`bin/raylib-libretro.c`](bin/raylib-libretro.c)).

## Controls

| Control            | Keyboard    |
| ---                | ---         |
| D-Pad              | Arrow Keys  |
| Buttons            | ZX AS QW    |
| Start              | Enter       |
| Select             | Right Shift |
| Open / Close Menu  | F1          |
| Save State         | F2          |
| Load State         | F4          |
| Screenshot         | F8          |
| Previous Shader    | F9          |
| Next Shader        | F10         |
| Fullscreen         | F11         |

Gamepad input is auto-detected — connect any controller before launching and it maps to the libretro JOYPAD device, including analog sticks and triggers. Press the gamepad's middle (Guide / Home) button to toggle the menu.

## Core Support

The following cores have been tested with raylib-libretro:

- fceumm
- snes9x
- picodrive

## Compile

[CMake](https://cmake.org) is used to build raylib-libretro.

``` sh
git clone http://github.com/robloach/raylib-libretro.git
cd raylib-libretro
git submodule update --init
mkdir build
cd build
cmake ..
make
bin/raylib-libretro ~/.config/retroarch/cores/fceumm_libretro.so smb.nes
```

### Windows

- Install [CMake](https://cmake.org/download/) and Visual Studio 2022 Build Tools (with the *Desktop development with C++* workload).
- From the project root:
  ``` sh
  git submodule update --init
  mkdir build
  cd build
  cmake .. -A x64
  cmake --build . --config Release
  ```
- The `-A x64` flag is important on machines where MSBuild defaults to ARM64. The output exe lands at `build\bin\Release\raylib-libretro.exe`.
- A `build.bat` helper at the repo root wraps the same commands.
- Cores from a Steam install of RetroArch live under `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores\`. Example:
  ``` sh
  build\bin\Release\raylib-libretro.exe ^
      "C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores\bsnes_libretro.dll" ^
      "C:\path\to\game.smc"
  ```

### macOS

- Make sure you have cmake / xcode-cli-tools installed.
- Run the above compile instructions.
- After installing RetroArch and some cores, you should be able to run:
    ```bash
    bin/raylib-libretro ~/Library/Application\ Support/RetroArch/cores/fceumm_libretro.dylib ~/Desktop/smb.nes
    ```

## Contributors

- [Konsumer](https://github.com/konsumer)
- [MikeDX](https://github.com/MikeDX) and [libretro-raylib](https://github.com/MikeDX/libretro-raylib)

## Shaders

The [`raylib-libretro-shaders.h`](include/raylib-libretro-shaders.h) header provides a family of retro post-process shaders. Include it with the implementation defined in one translation unit:

```c
#define RAYLIB_LIBRETRO_SHADERS_IMPLEMENTATION
#include "raylib-libretro-shaders.h"
```

### Available Shaders

| Type                    | Name                 | Description                                      |
| ---                     | ---                  | ---                                              |
| `SHADER_NONE`           | None                 | Pass-through, no post-processing                 |
| `SHADER_CRT`            | CRT                  | Barrel distortion, phosphor mask, corner vignette |
| `SHADER_SCANLINES`      | Scanlines            | Lightweight horizontal scanline overlay          |
| `SHADER_PIXELATE`       | Pixelate             | Chunky pixel-art block downscale                 |
| `SHADER_CHROMATIC_ABERR`| Chromatic Aberration | RGB channel split / lens fringing                |
| `SHADER_VIGNETTE`       | Vignette             | Darkened oval corners                            |
| `SHADER_BLOOM`          | Bloom                | Additive glow around bright areas                |
| `SHADER_GRAYSCALE`      | Grayscale            | Monochrome with optional tint                    |
| `SHADER_LCD_GRID`       | LCD Grid             | Subpixel LCD grid (Game Boy / GBA style)         |
| `SHADER_NTSC`           | NTSC                 | Composite signal artifacts: chroma bleed, noise  |
| `SHADER_COLOR_GRADE`    | Color Grade          | Hue, saturation, contrast, brightness, gamma     |

### Basic Usage

```c
LoadLibretroShaders();                        // load all shaders with defaults

while (!WindowShouldClose()) {
    UpdateLibretroShaders(GetFrameTime());    // cycle with F10/F9, update uniforms

    BeginDrawing();
        ClearBackground(BLACK);
        BeginLibretroShader();
            DrawLibretro();
        EndLibretroShader();
    EndDrawing();
}

UnloadLibretroShaders();
```

### Advanced Usage

Load a specific shader with custom parameters:

```c
ShaderCRTParams params = GetLibretroShaderDefaults(SHADER_CRT).params.crt;
params.curvatureRadius = 0.6f;
params.brightness      = 1.2f;
LibretroShaderState state = LoadLibretroShaderEx(SHADER_CRT, &params);
```

Tweak parameters at runtime via the active state pointer:

```c
LibretroShaderState *s = GetActiveLibretroShaderState();
if (s && s->type == SHADER_GRAYSCALE) {
    s->params.grayscale.tintColor = (Vector3){ 0.2f, 0.8f, 0.3f }; // Game Boy green
    UpdateLibretroShader(s, GetFrameTime());
}
```

Activate a shader programmatically:

```c
SetActiveLibretroShader(SHADER_NTSC);
// display current shader name in UI:
DrawText(GetLibretroShaderName(GetActiveLibretroShaderType()), 10, 10, 20, WHITE);
```

### API Reference

| Function | Description |
| --- | --- |
| `GetLibretroShaderCode(type)` | Returns the embedded GLSL source for the given type |
| `GetLibretroShaderDefaults(type)` | Returns a state populated with default parameters |
| `GetLibretroShaderName(type)` | Returns the display name (`"CRT"`, `"None"`, etc.) |
| `LoadLibretroShader(type)` | Compile shader with defaults |
| `LoadLibretroShaderEx(type, params)` | Compile shader with custom params |
| `UpdateLibretroShader(state, dt)` | Re-upload uniforms, accumulate time |
| `UnloadLibretroShader(state)` | Free GPU resource |
| `LoadLibretroShaders()` | Load all shaders with defaults |
| `UnloadLibretroShaders()` | Unload all shader GPU resources |
| `UpdateLibretroShaders(dt)` | Update active shader; F10 = next, F9 = previous |
| `CycleLibretroShader()` | Advance to next shader type |
| `CycleLibretroShaderReverse()` | Go back to previous shader type |
| `SetActiveLibretroShader(type)` | Activate a specific shader |
| `GetActiveLibretroShaderType()` | Returns the active `LibretroShaderType` |
| `GetActiveLibretroShaderState()` | Returns mutable pointer to active state, or NULL |
| `BeginLibretroShader()` | Begin shader mode (no-op when `SHADER_NONE`) |
| `EndLibretroShader()` | End shader mode (no-op when `SHADER_NONE`) |

## License

[zlib/libpng](LICENSE)
