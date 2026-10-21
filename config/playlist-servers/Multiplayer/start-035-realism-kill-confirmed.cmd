@echo off
setlocal
cd /d "%~dp0..\.."
if not exist "game_dx12_ship_replay.exe" (
    echo Keep this script in playlist-servers\Multiplayer inside your Replay installation.
    pause
    exit /b 1
)
rem Realism Kill Confirmed - native playlist 35. Map, script and rules come from the embedded .aggr.
rem Start one playlist server at a time on the configured matchmaking port.
rem --min-players 1 is for LAN testing; some modes require opponents to play a round.
start "IW8 Dedicated - 35 Realism Kill Confirmed" "%CD%\game_dx12_ship_replay.exe" --dedi --playlist 35 --playlist-rotate --min-players 1 -bap +seta lui_skip_season_video 1 %*
