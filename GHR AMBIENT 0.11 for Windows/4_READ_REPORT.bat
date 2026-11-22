@echo off
cd /d "%~dp0"
echo.
echo  TANK-G: READ GHR AMBIENT'S REPORT (levels, CPU, what happened). Nothing is written to the pedal.
echo  Play for a while first, then run this with the pedal still on.
echo  If Windows offers to FORMAT a disk, click CANCEL.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tankg_flash.ps1" status
echo.
pause
