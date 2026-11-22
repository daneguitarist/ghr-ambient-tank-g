@echo off
cd /d "%~dp0"
echo.
echo  TANK-G: INSTALL GHR AMBIENT. The first time, it backs up your pedal before writing anything.
echo  Writes ONLY the firmware area of the flash: your settings, amps/cabs and the bootloader are only
echo  read and checked, never written. 3_RESTORE_FACTORY puts the factory firmware back any time.
echo  Use the pedal's own USB cable. Don't unplug or switch off the pedal until it says DONE.
echo  If Windows offers to FORMAT a disk, click CANCEL.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tankg_flash.ps1" install
echo.
pause
