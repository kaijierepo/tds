REM ============================================================
REM TDS build pre-step: generate src/version.h (Windows)
REM - Fetch commit count from Gitee API (anonymous, header commit_count/total_count)
REM - Always generate src/version.h so compile never misses the file
REM - Only overwrite when content changed, to keep incremental build working
REM - No dependency on subwcrev.exe / version.temp.h template
REM ============================================================
@echo off
setlocal enabledelayedexpansion

set "VH=..\src\version.h"
set "HDR=%TEMP%\tds_gitee_hdr.txt"
set "NEW=%TEMP%\tds_version_h_new.txt"
set "REV=0"

REM 1. Fetch commit count from Gitee API (anonymous: commits response header)
curl -s -D "%HDR%" -o NUL --connect-timeout 5 --max-time 8 "https://gitee.com/api/v5/repos/liangtuSoft/tds/commits?per_page=1" >nul 2>nul
if exist "%HDR%" (
    for /f "usebackq tokens=1,* delims=:" %%a in ("%HDR%") do (
        if /i "%%a"=="commit_count" set "REV=%%b"
        if /i "%%a"=="total_count" set "REV=%%b"
    )
    del "%HDR%" >nul 2>nul
)

REM 2. Validate it is a pure number, otherwise fall back to 0
set "REV=!REV: =!"
echo !REV!| findstr /r /c:"^[0-9][0-9]*$" >nul 2>nul
if errorlevel 1 set "REV=0"

REM 3. Generate version.h; skip overwrite if content unchanged
REM GIT_VERSION 为裸数字 token（无引号），与 Linux build.sh 的 -DGIT_VERSION 注入保持一致，
REM 由 tds_imp.cpp 中 to_string(GIT_VERSION) 使用，失败回退 0
(
    echo #ifndef VERSION_H_
    echo #define VERSION_H_
    echo.
    echo #define GIT_VERSION !REV!
    echo.
    echo #endif
) > "%NEW%"

fc /b "%NEW%" "%VH%" >nul 2>nul
if errorlevel 1 (
    copy /y "%NEW%" "%VH%" >nul
    echo [preBuild] version.h updated: GIT_VERSION=!REV!
) else (
    echo [preBuild] version.h unchanged: GIT_VERSION=!REV!
)
del "%NEW%" >nul 2>nul
