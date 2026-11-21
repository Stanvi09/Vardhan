@echo off
rem Vardhan - build and run on Windows.
rem Needs gcc from w64devkit (https://github.com/skeeto/w64devkit/releases) on PATH.
rem raylib is bundled in third_party, so nothing else to install.

where gcc >nul 2>nul
if errorlevel 1 (
  echo gcc not found. Install w64devkit and add its bin folder to PATH.
  exit /b 1
)

set CORE=src\ds.c src\data.c src\profile.c src\risk.c src\advisor.c src\assistant.c src\backtest.c src\users.c src\portfolio.c src\alerts.c src\net.c third_party\cjson\cJSON.c
set APP=src\ui.c src\app.c src\scr_home.c src\scr_plan.c src\scr_ask.c src\scr_explore.c src\scr_friend.c src\scr_proof.c src\scr_hood.c src\scr_settings.c src\paper.c src\brand.c src\login.c src\scr_alerts.c src\report.c
set FLAGS=-std=c11 -O2 -Wall -Wno-format-truncation -Iinclude -Ithird_party\cjson -Ithird_party\raylib\include -D_POSIX_C_SOURCE=200809L

echo Building Vardhan...
windres assets\vardhan.rc -O coff -o vardhan_res.o || exit /b 1
gcc %FLAGS% %CORE% %APP% vardhan_res.o -Lthird_party\raylib\windows\lib -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32 -static -lpthread -mwindows -lm -o Vardhan.exe || exit /b 1

echo Building tests...
gcc %FLAGS% %CORE% src\tests.c -static -lpthread -lm -o Vardhan-Tests.exe || exit /b 1

echo Done. Starting Vardhan...
start "" Vardhan.exe
