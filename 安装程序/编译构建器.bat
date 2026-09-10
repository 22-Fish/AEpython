@echo off
rem ===========================================================================
rem  Build the builder itself:  <builder>.cpp  ->  <builder>.exe
rem
rem  This file is intentionally PURE ASCII.
rem  cmd.exe decodes .bat text with the console code page, so Chinese literals
rem  inside a batch file turn into garbage ("... is not recognized as an
rem  internal or external command").  Chinese file names are therefore picked
rem  up from the file system with wildcards instead of being hard-coded here.
rem
rem  MinGW's linker cannot handle non-ASCII paths either, so the compile runs
rem  inside an ASCII-only folder under %TEMP% and the result is copied back.
rem ===========================================================================
setlocal
set "HERE=%~dp0"
cd /d "%HERE%"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ was not found in PATH.
    echo         Install MinGW-w64, add its bin folder to PATH, then retry.
    pause
    exit /b 1
)

rem -- find the single builder source (.cpp) next to this script --------------
set "CPPSRC="
set "CPPNAME="
for %%f in ("%HERE%*.cpp") do (
    set "CPPSRC=%%~ff"
    set "CPPNAME=%%~nxf"
)
if not defined CPPSRC (
    echo [ERROR] no .cpp source file found next to this script.
    pause
    exit /b 1
)

rem -- header folder(s): keep the same relative layout in the build folder -----
set "HDRDIR="
for /d %%d in ("%HERE%*") do (
    for %%g in ("%%~fd\*.h") do set "HDRDIR=%%~fd"
)

set "BUILD=%TEMP%\AEpyBuilderBuild"
if exist "%BUILD%" rd /s /q "%BUILD%"
mkdir "%BUILD%"

echo [1/3] copying sources to %BUILD% ...
if defined HDRDIR (
    for /d %%d in ("%HERE%*") do (
        if exist "%%~fd\*.h" (
            mkdir "%BUILD%\%%~nxd" 2>nul
            copy /y "%%~fd\*.h" "%BUILD%\%%~nxd\" >nul
        )
    )
)
copy /y "%CPPSRC%" "%BUILD%\" >nul

echo [2/3] compiling the builder ...
pushd "%BUILD%"
g++ -std=c++17 -O2 -s -static -DUNICODE -D_UNICODE -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE -I. -o builder.exe "%CPPNAME%" -lole32 -loleaut32 -luuid -lshell32 -ladvapi32 -luser32 -lgdi32
set RC=%ERRORLEVEL%
popd

if not "%RC%"=="0" (
    echo [ERROR] compile failed.
    pause
    exit /b 1
)

echo [3/3] copying the result back ...
set "OUTBASE="
for %%f in ("%CPPSRC%") do set "OUTBASE=%%~nf"
copy /y "%BUILD%\builder.exe" "%HERE%%OUTBASE%.exe" >nul
if errorlevel 1 (
    echo [ERROR] could not write the output exe next to this script.
    pause
    exit /b 1
)

echo.
echo Done.  Builder created:
echo    %HERE%%OUTBASE%.exe
echo Run it to generate the installer (settings live in build.json).
pause
