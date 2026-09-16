@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo  MSYS2 + MinGW32 Installation Script
echo ============================================
echo.

:: Configuration
set "MSYS2_URL=https://github.com/msys2/msys2-installer/releases/download/2026-06-11/msys2-x86_64-20260611.exe"
set "INSTALLER_EXE=msys2_installer.exe"
set "INSTALL_DIR=C:\msys64"

:: Define the specific bins to add to PATH
set "USR_BIN=%INSTALL_DIR%\usr\bin"
set "MINGW_BIN=%INSTALL_DIR%\mingw32\bin"

:: Check if MSYS2 is already installed
if not exist "%USR_BIN%\bash.exe" (
    echo [1/4] Downloading MSYS2 installer...
    curl -L %MSYS2_URL% -o %INSTALLER_EXE%
    
    echo [2/4] Installing MSYS2 silently to %INSTALL_DIR%...
    start /wait "" %INSTALLER_EXE% in --confirm-command --accept-messages --root %INSTALL_DIR%
) else (
    echo MSYS2 already installed at %INSTALL_DIR%, skipping download and install.
)

echo [3/4] Installing MinGW32 and required packages...
"%USR_BIN%\bash.exe" -lc "pacman -Syu --noconfirm"
"%USR_BIN%\bash.exe" -lc "pacman -S --noconfirm mingw-w64-i686-toolchain mingw-w64-i686-cmake make"

echo [4/4] Adding MSYS2 and MinGW32 to User PATH...
:: Fetch current User PATH to avoid duplicates
for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v PATH') do set "OLD_PATH=%%B"

:: Append new paths only if they don't already exist in the string
echo %OLD_PATH% | find /i "%USR_BIN%" >nul || set "NEW_PATH=%USR_BIN%;"
echo %OLD_PATH% | find /i "%MINGW_BIN%" >nul || set "NEW_PATH=%NEW_PATH%%MINGW_BIN%;"

if defined NEW_PATH (
    setx PATH "%OLD_PATH%;%NEW_PATH%"
    echo PATH updated successfully.
) else (
    echo Paths already exist in Environment Variables.
)

:: Clean up installer
if exist "%INSTALLER_EXE%" del "%INSTALLER_EXE%"

echo.
echo ============================================
echo  Installation Complete!
echo  You may need to restart your terminal for
echo  PATH changes to take effect.
echo ============================================

pause