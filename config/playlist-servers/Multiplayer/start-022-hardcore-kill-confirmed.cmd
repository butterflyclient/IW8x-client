@echo off
setlocal
cd /d "%~dp0..\.."
if not exist "game_dx12_ship_replay.exe" (
    echo Keep this script in playlist-servers\Multiplayer inside your Replay installation.
    pause
    exit /b 1
)
rem Hardcore Kill Confirmed - native playlist 22. Map, script and rules come from the embedded .aggr.
rem Start one playlist server at a time on the configured matchmaking port.
rem --min-players 1 is for LAN testing; some modes require opponents to play a round.
start "IW8 Dedicated - 22 Hardcore Kill Confirmed" "%CD%\game_dx12_ship_replay.exe" --dedi --playlist 22 --playlist-rotate --min-players 1 -bap +seta lui_skip_season_video 1 %*
