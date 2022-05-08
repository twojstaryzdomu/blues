#!/bin/sh

PKGS="mingw64-cross-gcc mingw64-libgcc_s_seh1 mingw64-libmodplug mingw64-libmodplug-devel mingw64-libSDL2 mingw64-libSDL2-devel mingw64-libstdc++6 mingw64-libstdc++6 mingw64-libwinpthread1 mingw64-winpthreads-devel"
DLLS="libgcc_s_seh-1.dll libmodplug.dll libstdc++-6.dll libwinpthread-1.dll SDL2.dll"
BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"

rpm -q ${PKGS} 1>/dev/null || zypper install -y ${PKGS}
PATH=${BIN}:/usr/bin
which sdl2-config
mingw64-make
for l in ${DLLS}; do
  cp ${BIN}/${l} .
done
