@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo  KittenTools Windows Build Script
echo ============================================
echo.

:: Configuration
set "INSTALL_DIR=C:\msys64"
set "USR_BIN=%INSTALL_DIR%\usr\bin"
set "MINGW_BIN=%INSTALL_DIR%\mingw32\bin"

:: Get project root (script is at scripts\)
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%\.."
set "PROJECT_ROOT=%CD%"
popd

:: Parse arguments
set "CLEAN_BUILD=0"
set "RECONFIGURE=0"
set "DEBUG_BUILD=0"
for %%a in (%*) do (
    if /i "%%a"=="--clean" set "CLEAN_BUILD=1"
    if /i "%%a"=="-c" set "CLEAN_BUILD=1"
    if /i "%%a"=="--reconfigure" set "RECONFIGURE=1"
    if /i "%%a"=="-r" set "RECONFIGURE=1"
    if /i "%%a"=="--debug" set "DEBUG_BUILD=1"
    if /i "%%a"=="-d" set "DEBUG_BUILD=1"
)

:: Build output directory (use a separate dir so Debug/Release artifacts don't mix)
if "%DEBUG_BUILD%"=="1" (
    set "BUILD_DIR=%PROJECT_ROOT%\win-debug"
    set "BUILD_TYPE=Debug"
    echo [INFO] Debug build enabled (CMAKE_BUILD_TYPE=Debug^)
) else (
    set "BUILD_DIR=%PROJECT_ROOT%\win-release"
    set "BUILD_TYPE="
    echo [INFO] Release build
)

:: Convert paths to MSYS form for bash (forward slashes, /e/... drive form)
set "MSYS_ROOT=%PROJECT_ROOT:\=/%"
set "MSYS_ROOT=/%MSYS_ROOT::=%"
set "MSYS_BUILD=%BUILD_DIR:\=/%"
set "MSYS_BUILD=/%MSYS_BUILD::=%"

:: Check if MSYS2 is installed
if not exist "%USR_BIN%\bash.exe" (
    echo [ERROR] MSYS2 not found at %INSTALL_DIR%.
    echo Please run windows-install-msys2.bat first.
    pause
    exit /b 1
)

:: Check if MinGW32 toolchain is installed
if not exist "%MINGW_BIN%\i686-w64-mingw32-gcc.exe" (
    echo [ERROR] MinGW32 toolchain not found at %MINGW_BIN%.
    echo Please run windows-install-msys2.bat first.
    pause
    exit /b 1
)

:: Clean build if requested
if "%CLEAN_BUILD%"=="1" (
    if exist "%BUILD_DIR%" (
        echo [CLEAN] Removing previous build directory...
        rmdir /s /q "%BUILD_DIR%"
    )
)

:: Determine if we need to configure CMake
set "NEED_CONFIGURE=0"
if not exist "%BUILD_DIR%\CMakeCache.txt" set "NEED_CONFIGURE=1"
if "%RECONFIGURE%"=="1" set "NEED_CONFIGURE=1"

:: MSYS2 environment setup
set "CHERE_INVOKING=1"
set "MSYSTEM=MINGW32"

if "%NEED_CONFIGURE%"=="1" (
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
    echo [1/2] Configuring CMake...
    "%USR_BIN%\bash.exe" -lc "cd '%MSYS_ROOT%' && cmake -S . -B '%MSYS_BUILD%' -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
    if !ERRORLEVEL! NEQ 0 (
        echo [ERROR] CMake configuration failed.
        pause
        exit /b 1
    )
    echo.
    echo [2/2] Building...
) else (
    echo [1/1] Building (incremental^)...
)

"%USR_BIN%\bash.exe" -lc "cd '%MSYS_ROOT%' && cmake --build '%MSYS_BUILD%'"

if !ERRORLEVEL! NEQ 0 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Build Complete!
echo  Output: %BUILD_DIR%
echo ============================================

pause