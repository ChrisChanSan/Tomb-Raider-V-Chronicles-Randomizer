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
#        C:\msys64\usr\bin\bash.exe -lc "sh build-min.sh SRC BUILD PREFIX"
#      so that cl, link, lib and dumpbin of that VS are on PATH inside
#      bash. MSYS2 has its own /usr/bin/link.exe; configure's
#      compat/windows/mslink finds the MSVC one next to cl.exe.
#   2. This script, from that bash, with three MSYS paths, for example
#        sh build-min.sh /c/src/ffmpeg/src /c/src/ffmpeg/build /c/src/ffmpeg/out
#      SRC is the extracted snapshot (SRC/configure exists), BUILD the
#      build folder, PREFIX the install folder. The paths must carry no
#      blank: configure refuses an out-of-tree build whose source path
#      contains whitespace.
#
# The source tree stays untouched: configure runs in BUILD, make install
# writes PREFIX/bin (DLLs), PREFIX/lib (import libs) and PREFIX/include.
#
# --enable-decoder names the configure components: the Bink video decoder
# is "bink" there (its codec name is "binkvideo"). No --enable-parser: the
# Bink demuxer needs none, and --disable-everything leaves them all off.
# --disable-x86asm because there is no nasm; the Bink decoders are C.
# -d1trimfile cuts the source folder off __FILE__ (the av_assert strings),
# so the DLLs do not carry the folder the build ran in.

set -e

if [ $# -ne 3 ]; then
    echo "usage: sh build-min.sh <src> <build> <prefix>" >&2
    exit 2
fi
SRC=$1
BUILD=$2
PREFIX=$3
TRIM=$(cygpath -m "$SRC")/

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
    --enable-protocol=file \
    --extra-cflags="-d1trimfile:$TRIM"

make -j8
make install
