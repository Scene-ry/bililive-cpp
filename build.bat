@echo off
rem ============================================================
rem  build.bat - One-click build (MSVC first, MinGW fallback)
rem
rem  NOTE: keep this file pure ASCII (see build_mingw.bat).
rem ============================================================
cd /d "%~dp0"

where cl.exe >nul 2>nul
if not errorlevel 1 (
    echo MSVC environment detected; building with MSVC.
    call build_msvc.bat
    exit /b %errorlevel%
)

echo MSVC not detected; building with MinGW.
call build_mingw.bat
exit /b %errorlevel%
