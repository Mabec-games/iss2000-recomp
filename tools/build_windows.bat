@echo off
rem Windows build: clang-cl + Ninja inside the Visual Studio 2022 developer environment.
rem Needs VS 2022 (Build Tools) with "Desktop development with C++" and "C++ Clang tools for Windows", and Python 3.
rem usage: tools\build_windows.bat [build dir, default build-win]
setlocal
set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build-win

for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS_DIR=%%i
if "%VS_DIR%"=="" (
    echo Visual Studio 2022 with the C++ tools was not found.
    exit /b 1
)
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem RT64's shader preprocessing calls clang directly, so the LLVM tools must be on PATH.
set PATH=%VS_DIR%\VC\Tools\Llvm\x64\bin;%PATH%

cd /d "%~dp0.."
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1
