# FFmpeg, minimal - the Bink films of the 64-bit engine

The game's films are Bink files (`movie/*.bik`), and the only Bink library
the game brings, `binkw32.dll`, is 32-bit. The 64-bit engine cannot load it,
so its films are to be decoded by FFmpeg's own Bink decoders instead. This
folder holds those decoders as three DLLs, built from FFmpeg's source with
nothing else in them.

| File | Size | What it is |
|---|---|---|
| `bin/avutil-61.dll` | 991,744 B | libavutil 61.20.100 - memory, frames, log, CPU, float transforms |
| `bin/avcodec-63.dll` | 473,088 B | libavcodec 63.26.100 - decoders `bink`, `binkaudio_dct`, `binkaudio_rdft` |
| `bin/avformat-63.dll` | 410,112 B | libavformat 63.18.100 - demuxer `bink`, protocol `file` |
| `include/` | | the public headers, as `make install` wrote them |
| `build-min.sh` | | the exact configure line and the build recipe |
| `COPYING.LGPLv2.1` | | the licence, from the source tree |

No encoder, muxer, parser, filter, device, network code, scaler or
resampler is in them. They are linked dynamically - the engine loads them,
it does not contain them. Each DLL carries its own copy of the C runtime
(MSVC's default `/MT`, as FFmpeg's configure leaves it); they need nothing
beyond `KERNEL32.dll` and, for avutil, `bcrypt.dll`.

## Licence

FFmpeg built this way - without `--enable-gpl` and `--enable-nonfree` - is
under the **GNU Lesser General Public License, version 2.1 or later**; the
configure summary says "License: LGPL version 2.1 or later". The full text
is `COPYING.LGPLv2.1`. FFmpeg is a trademark of Fabrice Bellard, originator
of the FFmpeg project; the library's copyright belongs to the FFmpeg
developers.

## Source

The DLLs were built from the FFmpeg master snapshot of **2026-11-03**
(`FFmpeg-master.zip`, 24,050,402 bytes, SHA-256
`139de434067d59e5ccd40a0768a165c61c1e544d3ec2c930eaa5ce1ed5985e65`). Its
`RELEASE` file and configure call it `8.0.git`; the newest release section
of its `Changelog` is 9.1. The source tree was not modified. That snapshot
lies beside the release as `FFmpeg-master.zip` (24,050,402 bytes, SHA-256
`139de434067d59e5ccd40a0768a165c61c1e544d3ec2c930eaa5ce1ed5985e65`).

## How it was built

Visual Studio 18 (MSVC 14.51, x64) from an MSYS2 shell, out of tree, no
assembler (no nasm: `--disable-x86asm`; the Bink decoders are plain C).
`build-min.sh` holds the recipe and the configure line:

```
configure --toolchain=msvc --arch=x86_64 --target-os=win64
  --enable-shared --disable-static --disable-everything
  --disable-programs --disable-doc --disable-avdevice --disable-avfilter
  --disable-swscale --disable-swresample --disable-network
  --disable-autodetect --disable-x86asm --disable-debug
  --enable-demuxer=bink --enable-decoder=bink,binkaudio_dct,binkaudio_rdft
  --enable-protocol=file --extra-cflags=-d1trimfile:<src>/
```

The build ran out of tree in a neutral folder: the snapshot extracted to
`C:\src\ffmpeg\src`, then `sh build-min.sh /c/src/ffmpeg/src
/c/src/ffmpeg/build /c/src/ffmpeg/out`; `-d1trimfile` cuts the source folder
off the file names in the DLLs. `make install` also writes import
libraries; they are not kept here (the engine and `BinkProbe` load the DLLs
at run time and link none).

## Proof

`Preparation/Tools/Diagnose/BinkProbe.cpp` (`bbp.bat`) loads the three DLLs
at run time and decodes a film completely. On all nine films of the game
every video frame matches `ffmpeg -f framemd5` of a full FFmpeg 9.0.1 build,
and every audio frame matches it with that build's SIMD switched off
(`-cpuflags 0`); with SIMD the float audio differs in the last bits, as
FFmpeg's float transforms do. Decoding runs at 32x to 41x real time.
