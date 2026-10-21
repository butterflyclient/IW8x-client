@echo off
setlocal
rem Diagnostic launcher: uses the existing Island client setup verbatim.
rem Only affects this child process; never changes global environment variables.
set "IW8X_GSC_VM_PROFILE=1"
call "D:\Games\Call of Duty Modern Warfare (1.20.4.7623265)-001\Call of Duty Modern Warfare (1.20.4.7623265)\Call of Duty Modern Warfare (1.20.4.7623265)\start-island-client.cmd"
endlocal
