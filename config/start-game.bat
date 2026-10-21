@echo off
setlocal
cd /d "%~dp0"
if not exist "game_dx12_ship_replay.exe" (
    echo Place this script in the Replay game directory.
    pause
    exit /b 1
)
start "IW8 Client" "game_dx12_ship_replay.exe" +seta lui_skip_season_video 1
