# Licensing

GHR AMBIENT is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| `firmware/main.c`, `firmware/amb.h` (the ambient engine), `firmware/build.sh`, the installer (`tankg_flash.cs`, `tankg_flash.ps1`, the `.bat` files), `tools/fsim2.cs`, `README.md`, `KNOBS.txt` | Copyright (C) 2026 GerbilHamster Records |
| `firmware/crt0.S`, `firmware/app.ld` | GerbilHamster Records, following the entry and memory map of Felucca's FM-1 platform (same chip family) |
| `firmware/tg_usb.c`, `firmware/tg_usb_hw.h` (USB-MIDI) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's `src/usb.c` and `fm1_usb.h`, taken by way of MOTES 0.5 and changed for the Tank-G by GerbilHamster Records. GPL-3.0-only. |

`main.c` also follows Felucca's FM-1 platform in a few small hardware routines (the ADC read). The rest is
GerbilHamster Records' own, written from studying how the Tank-G works.

## Not included, and how the installer handles it

| What | Where it comes from |
| --- | --- |
| `wl82loader.bin`, JieLi's USB loader for the chip (it runs in the pedal's RAM while the installer works) | Downloaded when the installer runs, from [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) at commit `adb3f18`, path `data/loaderblobs/usb/wl82loader.bin`, and checked by SHA-256 `d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376` |
| M-VAVE's firmware, and the settings files in the pedal's firmware area (`cfg_tool.bin`, the `cfg` directory) | Never shipped. The installer reads them from your own pedal's backup and keeps them exactly where they are. Only the app is replaced. |
| JieLi's Linux toolchain (clang 4.0.1 for pi32v2) | Only used to build. The firmware links nothing from it: `-nostdlib`, and the build fails on any symbol that isn't ours. |
| JieLi's SDK | Not used in the build. Nothing from it is in this tree. |

JieLi's directory format, cipher and USB loader protocol were learned from kagaimiq's
[jielie](https://github.com/kagaimiq/jielie) notes and [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool)
(MIT). The installer's code is written from those notes; none of their code is copied in.

## USB ID

GHR AMBIENT shows up over USB as vendor `0x1209` (pid.codes), product `0x000A`, which is one of pid.codes' test
IDs.

## Trademarks

"M-VAVE" and "Tank-G" belong to their owners. "Felucca" and "Hügelton Instruments" are names of Hügelton
Instruments. "JieLi" belongs to Zhuhai Jieli Technology. GHR AMBIENT isn't made or endorsed by any of them.
