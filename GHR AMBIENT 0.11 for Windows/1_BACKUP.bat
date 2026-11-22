@echo off
cd /d "%~dp0"
echo.
echo  TANK-G: BACK UP YOUR PEDAL. Reads the whole flash twice and saves it in a backup_... folder.
echo  Nothing is written to the pedal. (2_INSTALL makes this backup by itself the first time, too.)
echo  Use the pedal's own USB cable. If Windows offers to FORMAT a disk, click CANCEL.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tankg_flash.ps1" backup
echo.
pause
