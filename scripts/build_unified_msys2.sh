#!/usr/bin/bash
# Builds the single-file tsMuxeR.exe (GUI + command line engine, no DLLs).
# Run from an MSYS2 UCRT64 shell with these packages installed:
#   mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,zlib,pkgconf,qt6-static,
#   libpng,freetype,harfbuzz,libjpeg-turbo,pcre2,zstd,double-conversion,md4c,
#   brotli,graphite2,bzip2,libwebp,libtiff,vulkan-headers}
set -e
cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DTSMUXER_STATIC_BUILD=ON \
  -DTSMUXER_UNIFIED=ON \
  -DWITHOUT_PKGCONFIG=OFF \
  -DQT_VERSION=6 \
  -DCMAKE_PREFIX_PATH="$MINGW_PREFIX/qt6-static"
cmake --build build

mkdir -p dist
strip -s -o dist/tsMuxeR.exe build/tsMuxerGUI/tsMuxeR.exe
echo "Built dist/tsMuxeR.exe"
