@echo off
rem ============================================================
rem  build_msvc.bat - MSVC 2022 build script
rem  Run from "x64 Native Tools Command Prompt for VS 2022"
rem  Output: build\msvc\BiliLiveCpp.exe
rem
rem  NOTE: keep this file pure ASCII (see build_mingw.bat).
rem ============================================================
setlocal
cd /d "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cl.exe not found.
    echo         Run this script from the "x64 Native Tools Command Prompt
    echo         for VS 2022", or run vcvarsall.bat x64 first.
    exit /b 1
)

if not exist build\msvc mkdir build\msvc

echo [1/3] Compiling resources (manifest)...
rc /nologo /fo build\msvc\app_res.res app.rc
if errorlevel 1 goto :fail

echo [2/3] Compiling and linking...
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE /Isrc ^
    src\main.cpp src\login_window.cpp src\main_window.cpp ^
    src\util.cpp src\json.cpp src\http.cpp src\session.cpp ^
    src\qrcode.cpp src\bili_api.cpp src\i18n.cpp ^
    build\msvc\app_res.res ^
    /Fe:build\msvc\BiliLiveCpp.exe ^
    /link /SUBSYSTEM:WINDOWS ^
    winhttp.lib bcrypt.lib crypt32.lib gdi32.lib user32.lib ole32.lib shell32.lib comctl32.lib
if errorlevel 1 goto :fail

echo [3/3] Done.
echo.
echo Build succeeded: build\msvc\BiliLiveCpp.exe
exit /b 0

:fail
echo.
echo [ERROR] Build failed. See output above.
exit /b 1
