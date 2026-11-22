# GHR AMBIENT: custom firmware for the M-VAVE Tank-G

**An ambient board for the M-VAVE Tank-G v2 multi-effects pedal.** By GerbilHamster Records.

GHR AMBIENT replaces the Tank-G's firmware with one big instrument for long, slow, drifting guitar. You get a
granular **cloud** that freezes what you just played, or follows you live, and slowly **rots**. There's a huge
**reverb**, a dark echo that never attacks (your notes **smear** and swell into it), and an old-tape **warble**.
Everything trembles a little, on purpose.

> **Status: 0.11.** Made and played on one Tank-G v2 (factory firmware `TANK-Gv2_097`). The installer is
> Windows only. This is a hobby project. It isn't made, supported or endorsed by M-VAVE / Cuvave.
> **You use it at your own risk.**

## What it does

Everything is off at power-up. The knobs keep their panel labels, but most of them do something new:

| Control | What it does now |
| --- | --- |
| **A** tap | **GRAB**: freezes the last few seconds into a cloud (pale blue, blooms in over ~4 s). Tap again: it fades away (amber). |
| **A** hold | **LIVE CLOUD** (bright purple): the cloud follows whatever you play, a little behind you. Tap: freeze it there. Hold: fade it out. |
| **B** tap | **SPACE**: a huge reverb (1.5 s to a minute) |
| **B** hold | **WET ONLY**: the dry guitar fades out (ring magenta). Hold again to bring it back. |
| **C** | **SMEAR**: a dark echo your notes swell into, each repeat blurrier. Now and then its time gets a tiny tug. |
| **D** | **WARBLE**: everything through an old tape, with slow random pitch drift (no chorus, no tremolo) |
| MASTER | output level (everything is summed, then MASTER, then a soft limiter: nothing hard-clips) |
| RVB DECAY / RVB MIX | reverb length / level |
| MOD SPEED | **DRIFT**: how far and fast the cloud wanders (all the way down = still) |
| IR CAB | **SIZE**: grain length (small = sparkly, big = smeared) |
| DLY MIX / DLY TIME | SMEAR level / time (turning the time bends the repeats) |
| MOD FX | **NERVE**: wobble, wow, a little flutter, slow smooth dips |
| VOLUME | **CLOUD**: cloud level |
| GAIN | input level (noon = your guitar as it comes) |
| MIDDLE | **START**: where in the memory the cloud sits; with a live cloud, how far behind you it trails |
| NOISE GATE | **SWELL**: how slowly notes fade into SMEAR |
| BASS | **ROT**: how fast a frozen memory rots (all the way down = never) |
| TYPE | **PITCH**: -oct, -oct + unison, unison, unison + 5th + oct, +oct |
| TREBLE | **TONE**: dark to open, for everything wet |

While A is going, **the bender** quietly adds a clone now and then: half speed, backwards, double speed, a tape
stop, or a little piece repeated. Always on, with no knobs: a gentle compressor, a little EQ (more bass, softer
highs) and a hiss reducer. The full list is in [`KNOBS.txt`](KNOBS.txt).

## Install (Windows 10 or 11)

1. **Download**: the green **Code** button, then **Download ZIP**. Unzip it somewhere easy, like your Desktop.
2. Open the folder **GHR AMBIENT 0.11 for Windows**.
3. Plug the Tank-G into the computer with its USB cable and switch it on (factory firmware running).
4. Double-click **`2_INSTALL.bat`**. The first time, before it writes anything, it:
   - downloads JieLi's small USB loader (`wl82loader.bin`) from
     [kagaimiq's jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) and checks its fingerprint;
   - reads your pedal's whole flash twice, checks both reads match, and saves them in a **`backup_<date>`** folder.
     **Keep that folder, and copy it somewhere safe.** It's your only way back to the factory firmware.
5. Type **INSTALL** when it asks. If Windows offers to *format* a disk, click **Cancel**: that's the pedal's loader
   showing up as a disk. Don't unplug the pedal until the window says **DONE**.
6. Unplug the pedal, wait 5 seconds, plug it back in. Play.

If Windows asks whether to run a downloaded file, choose *Run*. If it shows *Windows protected your PC*, click
*More info*, then *Run anyway*. The scripts are plain text: open `tankg_flash.cs` to see exactly what they do.

### The other buttons

- **`1_BACKUP.bat`**: makes the backup on its own, without installing anything. It only reads.
- **`3_RESTORE_FACTORY.bat`**: puts the factory firmware back from your backup. The `backup_...` folder must be
  next to it.
- **`4_READ_REPORT.bat`**: after you've played a while, reads GHR AMBIENT's report: input and output levels, CPU
  load, what the cloud did. It only reads.

## Safety

- Only the firmware area of the flash (`0x4000`–`0x7AFFF`) is ever erased or written. The bootloader, your
  settings, M-VAVE's amps and cabs, and everything else are only read. They're checked against your backup before
  anything is written. Every sector is read back after it's written, and the whole flash is read and checked again
  at the end.
- The firmware directory is erased first and written last. If an install stops halfway (a cable falls out), the
  pedal starts in its USB loader next time, and you just run the install or the restore again.
- To get into the loader by hand: unplug the pedal, **hold footswitch A**, and plug it back in (keep holding for 2 s).
  While GHR AMBIENT is playing, **A + D held for 2 s** does the same.
- The firmware area is built on *your* computer from your own pedal's backup plus our app
  (`ghr_ambient_app.bin`), so the installer never carries anything of M-VAVE's.

## What's not in here

- M-VAVE's firmware, or anyone's pedal backup. Your backup stays on your computer. Don't share it: it holds
  M-VAVE's firmware and your pedal's own settings.
- JieLi's USB loader. It's downloaded when the installer runs, from one fixed version of kagaimiq's jl-uboot-tool,
  and checked by fingerprint.
- JieLi's SDK and toolchain. The toolchain is only used to build. Nothing from it or the SDK is linked in.

## Building the firmware

See [`firmware/README.md`](firmware/README.md). You need Linux (or WSL) and JieLi's toolchain. The build makes
`ghr_ambient_app.bin`. The one in the Windows folder was built from exactly this source. Its SHA-256 is
`cf8cef6c78438eea8b2d33608a51464397ee104e02500df74ddbebbd53350d16`.

## Licence and credits

GHR AMBIENT is free software under the **GNU General Public License, version 3 only** (`GPL-3.0-only`, full text
in [`LICENSE`](LICENSE)). If you share a firmware built from it, share its complete source under the same licence.
Who made what is in [`LICENSING.md`](LICENSING.md).

- The USB-MIDI driver comes from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton
  Instruments), the custom firmware for M-VAVE's FM-1, by way of GerbilHamster Records' MOTES.
- JieLi's flash formats and the USB loader protocol were learned from
  [kagaimiq's jielie notes and jl-uboot-tool](https://github.com/kagaimiq/jielie).
- "M-VAVE" and "Tank-G" belong to their owners. "Felucca" and "Hügelton Instruments" are names of Hügelton
  Instruments. GHR AMBIENT isn't made or endorsed by any of them.

## Like it?

GHR AMBIENT is free. If it earned a spot on your board and you feel like it, you can
[buy GerbilHamster Records a coffee on Ko-fi](https://ko-fi.com/gerbilhamsterrecords). Never expected, always
appreciated.
