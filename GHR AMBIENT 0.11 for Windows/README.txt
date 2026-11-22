GHR AMBIENT 0.11 - custom firmware for the M-VAVE Tank-G v2 (GerbilHamster Records). Windows 10/11.
At your own risk. Not made or endorsed by M-VAVE.

INSTALL
  1. Plug the Tank-G in with its USB cable, switch it on.
  2. Double-click 2_INSTALL.bat. The first time it downloads JieLi's USB loader (checked by fingerprint)
     and BACKS UP your pedal into a backup_<date> folder before writing anything.
     KEEP THAT FOLDER (copy it somewhere safe): it's your only way back to the factory firmware.
  3. Type INSTALL when asked. If Windows offers to FORMAT a disk, click CANCEL.
     Don't unplug the pedal until it says DONE. Then unplug, wait 5 s, plug back in.

OTHER BUTTONS
  1_BACKUP.bat           make the backup only (reads, writes nothing)
  3_RESTORE_FACTORY.bat  factory firmware back, from your backup_... folder (keep it next to these files)
  4_READ_REPORT.bat      after playing: levels, CPU, what the cloud did (reads, writes nothing)

If something stops halfway: leave the pedal plugged in and run 3_RESTORE_FACTORY.bat (or 2_INSTALL.bat) again.
The pedal's bootloader is never touched, so it can always get back to its USB loader:
unplug, HOLD footswitch A, plug in, keep holding 2 s.

What the knobs and switches do: KNOBS.txt. Source, licence (GPL-3.0-only) and credits: the project's GitHub page.
