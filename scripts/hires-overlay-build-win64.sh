#!/bin/bash
set -e
# Cross-build static Windows x64 deps for OXCE (Debian/Ubuntu: g++-mingw-w64-x86-64-posix, cmake, make, pkg-config).
# Sources expected in $S: zlib-1.3.1 libpng-1.6.44 libogg-1.3.5 libvorbis-1.3.7 flac-1.4.3 freetype-2.13.3
#   SDL-1.2-main (github libsdl-org/SDL-1.2) SDL_image-1.2.12 SDL_mixer-1.2.12 SDL_gfx-2.0.26
H=x86_64-w64-mingw32
P=/workspace/oxce/wdeps/prefix
S=/workspace/oxce/wdeps/src
B=/workspace/oxce/wdeps/build
mkdir -p $P $B
export CC=$H-gcc-posix CXX=$H-g++-posix AR=$H-ar RANLIB=$H-ranlib WINDRES=$H-windres STRIP=$H-strip
export PKG_CONFIG_LIBDIR=$P/lib/pkgconfig PKG_CONFIG_PATH=
export CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib" CFLAGS="-O2"
TC=$B/toolchain.cmake
cat > $TC <<T
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $H-gcc-posix)
set(CMAKE_CXX_COMPILER $H-g++-posix)
set(CMAKE_RC_COMPILER $H-windres)
set(CMAKE_FIND_ROOT_PATH $P /usr/$H)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
T
step(){ echo "=== $1"; }
AC="--host=$H --prefix=$P --disable-shared --enable-static"

step zlib
cd $S/zlib-1.3.1 && make -f win32/Makefile.gcc PREFIX=$H- CC=$CC clean >/dev/null 2>&1 || true
make -f win32/Makefile.gcc PREFIX=$H- CC=$CC -j8 libz.a >/dev/null
make -f win32/Makefile.gcc PREFIX=$H- CC=$CC install BINARY_PATH=$P/bin INCLUDE_PATH=$P/include LIBRARY_PATH=$P/lib SHARED_MODE=0 >/dev/null

step libpng
rm -rf $B/png && mkdir $B/png && cd $B/png
cmake $S/libpng-1.6.44 -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_BUILD_TYPE=Release -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DZLIB_ROOT=$P >/dev/null
make -j8 install >/dev/null

step ogg
cd $S/libogg-1.3.5 && ./configure $AC >/dev/null && make -j8 >/dev/null && make install >/dev/null
step vorbis
cd $S/libvorbis-1.3.7 && ./configure $AC --disable-docs --disable-examples >/dev/null && make -j8 >/dev/null && make install >/dev/null
step flac
rm -rf $B/flac && mkdir $B/flac && cd $B/flac
cmake $S/flac-1.4.3 -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_CXXLIBS=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF -DWITH_OGG=ON >/dev/null
make -j8 install >/dev/null

step freetype
rm -rf $B/ft && mkdir $B/ft && cd $B/ft
cmake $S/freetype-2.13.3 -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DFT_REQUIRE_ZLIB=ON -DFT_REQUIRE_PNG=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON >/dev/null
make -j8 install >/dev/null

step SDL
cd $S/SDL-1.2-main && ./configure $AC --disable-stdio-redirect >/dev/null && make -j8 >/dev/null && make install >/dev/null
export SDL_CONFIG=$P/bin/sdl-config
step SDL_image
cd $S/SDL_image-1.2.12 && CFLAGS="-O2 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion" ./configure $AC --with-sdl-prefix=$P --enable-png --disable-png-shared --disable-jpg --disable-tif --disable-webp >/dev/null && make -j8 >/dev/null && make install >/dev/null
step SDL_mixer
cd $S/SDL_mixer-1.2.12 && CPPFLAGS="$CPPFLAGS -DFLAC__NO_DLL" CFLAGS="-O2 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion" LIBS="-lvorbis -logg -lws2_32" ./configure $AC --with-sdl-prefix=$P --enable-music-ogg --disable-music-ogg-shared --enable-music-flac --disable-music-flac-shared --disable-music-mod --disable-music-mp3 --enable-music-midi --enable-music-native-midi --disable-music-timidity-midi --disable-music-fluidsynth-midi >/dev/null && make -j8 >/dev/null && make install >/dev/null
step SDL_gfx
cd $S/SDL_gfx-2.0.26 && ./configure $AC --with-sdl-prefix=$P --disable-mmx >/dev/null && make -j8 >/dev/null && make install >/dev/null
step openxcom
mkdir -p $B/oxce && cd $B/oxce
cmake ${OXCE_SRC:-/workspace/oxce/src} -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release -DDEV_BUILD=OFF -DBUILD_PACKAGE=OFF -DDEPS_DIR=/nonexistent \
  -DCMAKE_CXX_STANDARD_LIBRARIES="-lvorbisfile -lvorbis -lFLAC -logg -lpng16 -lz -lwinmm -ldxguid -lgdi32 -luser32 -lole32 -loleaut32 -limm32 -lversion -luuid -lopengl32 -lws2_32"
make -j8
echo ALLDONE
