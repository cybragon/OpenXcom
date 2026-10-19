#!/bin/bash
# battle-8: libwebp 1.4.0 static + SDL_image 1.2.12 rebuilt with JPG + WEBP (static, not dlopen)
set -e
H=x86_64-w64-mingw32; P=/workspace/oxce/wdeps/prefix; S=/workspace/oxce/wdeps/src; B=/workspace/oxce/wdeps/build
export CC=$H-gcc-posix CXX=$H-g++-posix AR=$H-ar RANLIB=$H-ranlib WINDRES=$H-windres STRIP=$H-strip
export PKG_CONFIG_LIBDIR=$P/lib/pkgconfig PKG_CONFIG_PATH=
export CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib" CFLAGS="-O2"
TC=$B/toolchain.cmake
cd $S && rm -rf libwebp-1.4.0 && tar xzf libwebp-1.4.0.tar.gz
rm -rf $B/webp && mkdir $B/webp && cd $B/webp
cmake $S/libwebp-1.4.0 -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
 -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF \
 -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF -DWEBP_USE_THREAD=OFF
make -j8 >/dev/null
make install >/dev/null
export CFLAGS="-O2 -Wno-error=incompatible-pointer-types" SDL_CONFIG=$P/bin/sdl-config LIBS="-lsharpyuv"
# SDL_image 1.2.12 declares the libwebp entry points with pre-0.2 types (uint32_t/int sizes): use size_t (Win64 ABI)
sed -i -e 's/uint32_t data_size/size_t data_size/g' -e 's/int output_buffer_size/size_t output_buffer_size/g' -e 's/(const uint8_t \*, uint32_t,/(const uint8_t *, size_t,/g' $S/SDL_image-1.2.12/IMG_webp.c
cd $S/SDL_image-1.2.12 && (make distclean >/dev/null 2>&1 || true)
./configure --host=$H --prefix=$P --disable-shared --enable-static --with-sdl-prefix=$P --enable-png --disable-png-shared --enable-jpg --disable-jpg-shared --disable-tif --enable-webp --disable-webp-shared >/dev/null
grep -n "LOAD_WEBP\|LOAD_JPG" config.log | head -4 || true
make -j8 >/dev/null
make install >/dev/null
$H-nm $P/lib/libSDL_image.a | grep -c " U WebPDecode" || true
$H-nm $P/lib/libSDL_image.a | grep -c " U jpeg_read_header" || true
echo WEBPDONE
