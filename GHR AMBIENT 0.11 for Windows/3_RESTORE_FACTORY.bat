@echo off
cd /d "%~dp0"
echo.
echo  TANK-G: RESTORE THE FACTORY FIRMWARE from your backup. Writes ONLY the firmware area of the flash.
echo  If GHR AMBIENT is on the pedal and it isn't answering over USB: when asked, unplug the pedal,
echo  HOLD footswitch A, plug it back in, keep holding A for 2 seconds.
echo  Don't unplug or switch off the pedal until it says DONE. If Windows offers to FORMAT a disk, click CANCEL.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tankg_flash.ps1" restore
echo.
pause
