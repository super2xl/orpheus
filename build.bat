@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

:: Check for cmake in PATH
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "PATH=C:\Program Files\CMake\bin;%PATH%"
        echo Using cmake from C:\Program Files\CMake\bin
    ) else (
        echo Error: cmake not found in PATH or default install location
        exit /b 1
    )
)

:: Step 1: Build C++ core DLL
echo === Building C++ core ===
if not exist build mkdir build
cmake -S . -B build 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: CMake configure failed
    exit /b 1
)
cmake --build build --config Release --target orpheus_core 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: C++ build failed
    exit /b 1
)
echo C++ core built successfully

:: Step 2: Copy DLL to Tauri target directory
echo === Copying DLL ===
if not exist "ui\src-tauri\target\release" mkdir "ui\src-tauri\target\release"
copy /y "build\bin\Release\orpheus_core.dll" "ui\src-tauri\target\release\" >nul
if %ERRORLEVEL% neq 0 (
    echo Error: DLL copy failed
    exit /b 1
)
echo DLL copied to ui\src-tauri\target\release\

:: Step 3: Build frontend
echo === Building frontend ===
cd ui
call npm run build 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: Frontend build failed
    exit /b 1
)
echo Frontend built successfully

:: Step 4: Build Tauri app
echo === Building Tauri app ===
call npx tauri build 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: Tauri build failed
    exit /b 1
)
echo Tauri app built successfully

cd /d "%~dp0"
echo === Build complete ===
