#!/bin/sh
# build-min.sh - the minimal FFmpeg of the 64-bit engine's films (#315).
#
# Three DLLs (avutil, avcodec, avformat), LGPL v2.1 or later, shared only,
# with nothing in them but the Bink demuxer, the Bink video decoder, the two
# Bink audio decoders and the file protocol.
#
# Source: the FFmpeg master snapshot of 2026-11-03 (FFmpeg-master.zip).
# configure / ffbuild/version.sh report it as "8.0.git" (RELEASE file);
# the Changelog head is "version <next>", the newest release section 9.1.
#
# Recipe (MSVC of Visual Studio 18 from an MSYS2 shell, no nasm, no perl,
# no pkg-config needed):
#
#   1. cmd.exe:
#        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\
#              Auxiliary\Build\vcvars64.bat"
#        set MSYS2_PATH_TYPE=inherit
#        C:\msys64\usr\bin\bash.exe -lc "sh /c/CLOUDE~1/.../build-min.sh"
#      so that cl, link, lib and dumpbin of that VS are on PATH inside
#      bash. MSYS2 has its own /usr/bin/link.exe; configure's
#      compat/windows/mslink finds the MSVC one next to cl.exe.
#   2. This script, from that bash. It needs the paths below WITHOUT
#      blanks: configure refuses an out-of-tree build whose source path
#      contains whitespace, so "C:\Cloude AI" is reached by its 8.3 name
#      CLOUDE~1 (check with "dir /x C:\").
#
# The source tree stays untouched: configure runs in BUILD, make install
# writes PREFIX/bin (DLLs), PREFIX/lib (import libs) and PREFIX/include.
#
# --enable-decoder names the configure components: the Bink video decoder
# is "bink" there (its codec name is "binkvideo"). No --enable-parser: the
# Bink demuxer needs none, and --disable-everything leaves them all off.
# --disable-x86asm because there is no nasm; the Bink decoders are C.

set -e

SRC=/c/CLOUDE~1/FFmpeg/FFmpeg-master
BUILD=/c/CLOUDE~1/FFmpeg/build-min
PREFIX=/c/CLOUDE~1/FFmpeg/out-min

mkdir -p "$BUILD"
cd "$BUILD"

"$SRC/configure" --toolchain=msvc --arch=x86_64 --target-os=win64 \
    --prefix="$PREFIX" --enable-shared --disable-static \
    --disable-everything --disable-programs --disable-doc \
    --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-swresample --disable-network --disable-autodetect \
    --disable-x86asm --disable-debug \
    --enable-demuxer=bink \
    --enable-decoder=bink,binkaudio_dct,binkaudio_rdft \
    --enable-protocol=file

make -j8
make install
