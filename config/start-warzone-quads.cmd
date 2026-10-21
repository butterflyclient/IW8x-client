@echo off
setlocal
cd /d "%~dp0"
if not exist "game_dx12_ship_replay.exe" (
    echo Place this script in the Replay game directory.
    pause
    exit /b 1
)
rem Compatibility name; all playlists now have their own generated launcher.
call "%~dp0playlist-servers\Warzone\start-089-br-quads.cmd" %*
