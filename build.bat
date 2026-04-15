@echo off
setlocal
if not exist build mkdir build
cmake -S . -B build || exit /b 1
cmake --build build --config Debug || exit /b 1
echo.
echo Built: build\bin\Debug\raylib-libretro.exe
