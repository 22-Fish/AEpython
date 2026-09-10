@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [错误] 未找到 gcc，请先安装 MinGW-w64 并把 bin 目录加入 PATH。
    pause
    exit /b 1
)

where windres >nul 2>nul
if errorlevel 1 (
    echo [错误] 未找到 windres，它通常随 MinGW-w64 一起安装。
    pause
    exit /b 1
)

echo [1/2] 编译资源（图标 + 应用程序清单 + 版本信息）...
windres launcher.rc -O coff -o launcher_res.o
if errorlevel 1 (
    echo [错误] 资源编译失败。
    pause
    exit /b 1
)

echo [2/2] 编译启动器 AEpy.exe（无控制台）...
gcc -O2 -s -mwindows -o AEpy.exe launcher.c launcher_res.o
if errorlevel 1 (
    echo [错误] 编译失败。
    pause
    exit /b 1
)

del /q launcher_res.o >nul 2>nul

echo.
echo 构建完成：%~dp0AEpy.exe
echo 双击 AEpy.exe 即可启动 AEpy（不会出现控制台窗口）。
pause
