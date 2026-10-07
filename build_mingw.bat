@echo off
rem ============================================================
rem  build_mingw.bat - MinGW-w64 (MSYS2) single-file build script
rem  Output: build\BiliLiveCpp.exe (static, zero runtime deps)
rem
rem  NOTE: keep this file pure ASCII. cmd.exe corrupts batch-file
rem        parsing when the file contains multibyte characters
rem        (UTF-8/GBK), causing lines to be skipped or truncated.
rem ============================================================
setlocal
cd /d "%~dp0"

rem Add MinGW toolchain to PATH (cc1plus needs its DLLs, else silent fail)
set "PATH=C:\msys64\mingw64\bin;%PATH%"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found. Please install the MSYS2 mingw-w64 toolchain.
    exit /b 1
)

if not exist build mkdir build

echo [1/3] Compiling resources (manifest)...
windres --input app.rc --output build\app_res.o --output-format=coff
if errorlevel 1 goto :fail

echo [2/3] Compiling and linking...
g++ -std=c++17 -O2 -mwindows -static -static-libgcc -static-libstdc++ ^
    -Wall -Wextra ^
    -Isrc ^
    src\main.cpp src\login_window.cpp src\main_window.cpp ^
    src\util.cpp src\json.cpp src\http.cpp src\session.cpp ^
    src\qrcode.cpp src\bili_api.cpp src\i18n.cpp ^
    build\app_res.o ^
    -o build\BiliLiveCpp.exe ^
    -lwinhttp -lbcrypt -lcrypt32 -lgdi32 -luser32 -lole32 -lshell32 -lcomctl32
if errorlevel 1 goto :fail

echo [3/3] Done.
echo.
echo Build succeeded: build\BiliLiveCpp.exe
echo Run with --selftest to verify; --qr-dump to dump a QR matrix.
exit /b 0

:fail
echo.
echo [ERROR] Build failed. See output above.
exit /b 1
