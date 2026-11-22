# Building GHR AMBIENT

The firmware is bare-metal C for the Tank-G's JieLi AC791N-family chip (pi32v2 core). It uses fixed-point DSP, has no
OS, no SDK and no libraries, and runs straight from flash.

| File | What it is |
| --- | --- |
| `main.c` | Start-up, clocks, the audio codec link (I2S), knobs, footswitches, ring lights, the status block, the "go to loader" escape hatches |
| `amb.h` | The ambient engine: GRAB / live cloud / rot / the bender, SPACE (reverb), SMEAR, WARBLE, NERVE, compressor, EQ |
| `tg_usb.c`, `tg_usb_hw.h` | USB-MIDI (from Felucca): answers the identity request and takes the "go to loader" message |
| `crt0.S`, `app.ld` | Entry point and memory map |
| `build.sh` | The build |

## Build (Linux or WSL)

```sh
mkdir -p ~/.jieli && curl -fL https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C ~/.jieli
export JIELI_TOOLCHAIN=$(ls -d ~/.jieli/jieli-linux-toolchains-* | sort | tail -1)
./build.sh            # -> build/ghr_ambient_app.bin
```

Built with `jieli-linux-toolchains-20260730.1`, this source gives exactly the `ghr_ambient_app.bin` in the Windows
folder: 24764 bytes, SHA-256 `cf8cef6c78438eea8b2d33608a51464397ee104e02500df74ddbebbd53350d16`.

## Installing your own build

The installer only takes the app whose fingerprint it knows. After changing the firmware, copy your
`build/ghr_ambient_app.bin` into the Windows folder and put its SHA-256 into `tankg_flash.cs`, in the line
`public static readonly string OURS_SHA = "...";`. Also change the version in `OURS_VER`, and in the identity
string `TG_ID_NAME` in `tg_usb.c`.

Before writing anything to a pedal, you can try the installer's logic against your own backup with
`tools/fsim2.cs`, a simulated pedal (Mono: `mcs -langversion:5 -target:library tankg_flash.cs`, then compile
`fsim2.cs` against it, and run it with your `flash_full_1MiB.bin`, your app, a reference area image and a scratch
folder).

## Things to know

- The bootloader maps only `.text` (code and constants). `.data` must stay empty: the build checks it.
- Every symbol must be ours (`-nostdlib`). The build stops on anything undefined. So there's no 64-bit division,
  no float, and no libc.
- Hold footswitch A while plugging in, or hold A + D while it runs: the firmware jumps to the chip's USB loader, so
  a bad build can always be replaced.
- The firmware leaves a status block in RAM (CPU load, levels, what the cloud did) that `4_READ_REPORT.bat` reads
  through the loader.
