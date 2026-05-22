@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "CLEAN=0"
set "SKIP_WEB=0"
set "SKIP_CPP=0"

:parse
if "%~1"=="" goto :main
if /i "%~1"=="--clean"   set "CLEAN=1"
if /i "%~1"=="--skip-web" set "SKIP_WEB=1"
if /i "%~1"=="--skip-cpp" set "SKIP_CPP=1"
shift
goto :parse

:main
echo ============================================
echo   ChargerLogApp 打包脚本
echo ============================================

:: ── 清理 ────────────────────────────────────────
if %CLEAN%==1 (
    echo.
    echo [清理] 删除旧构建产物...
    if exist "%ROOT%dist\ChargerLogApp" rmdir /s /q "%ROOT%dist\ChargerLogApp"
    if exist "%ROOT%build" rmdir /s /q "%ROOT%build"
    if exist "%ROOT%web\dist" rmdir /s /q "%ROOT%web\dist"
)

:: ── C++ 编译 ────────────────────────────────────
if %SKIP_CPP%==1 (
    echo.
    echo [跳过] C++ 编译
) else (
    echo.
    echo [1/3] 编译 C++ chargerlog...

    if not exist "%ROOT%core\build" mkdir "%ROOT%core\build"
    pushd "%ROOT%core\build"

    if not exist CMakeCache.txt (
        cmake -G "MinGW Makefiles" ..
    )
    cmake --build . --config Release
    popd
    echo   -^> chargerlog.exe 编译完成
)

:: ── 前端构建 ────────────────────────────────────
if %SKIP_WEB%==1 (
    echo.
    echo [跳过] 前端构建
) else (
    echo.
    echo [2/3] 构建 Vue 前端...

    pushd "%ROOT%web"
    if not exist node_modules (
        echo   安装前端依赖...
        call npm install
    )
    call npm run build
    popd
    echo   -^> web/dist/ 构建完成
)

:: ── PyInstaller 打包 ────────────────────────────
echo.
echo [3/3] PyInstaller 打包...

set "WEB_DIST=%ROOT%web\dist"
set "CHARGERLOG_EXE=%ROOT%core\build\chargerlog.exe"

if not exist "%WEB_DIST%" (
    echo   ERROR: web\dist\ 不存在，请先构建前端或去掉 --skip-web
    exit /b 1
)
if not exist "%CHARGERLOG_EXE%" (
    echo   ERROR: chargerlog.exe 不存在，请先编译 C++ 或去掉 --skip-cpp
    exit /b 1
)

pushd "%ROOT%"
pyinstaller chargerlog.spec
popd

:: 清理 COLLECT 残留的中间 EXE
if exist "%ROOT%dist\ChargerLogApp.exe" del /q "%ROOT%dist\ChargerLogApp.exe"

echo.
echo ============================================
echo   打包完成
echo   输出: %ROOT%dist\ChargerLogApp
echo   可执行文件: %ROOT%dist\ChargerLogApp\ChargerLogApp.exe
echo ============================================
