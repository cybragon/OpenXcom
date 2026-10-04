#!/bin/bash
# Stage + zip the Windows x64 package of the hi-res overlay fork (NO original game data, NO fonts).
# usage: hires-overlay-package-win64.sh EXE OUTZIP   (EXE = openxcom.exe built by hires-overlay-build-win64.sh)
set -e
EXE=$1; OUT=$2
R=$(cd "$(dirname "$0")/.." && pwd)            # source tree
S=${DEPS_SRC:-/workspace/oxce/wdeps/src}         # dependency sources (license texts)
T=$(mktemp -d); P=$T/Extended-8.7.1-hires-1
mkdir -p $P/UFO $P/TFTD $P/licenses $P/user
x86_64-w64-mingw32-strip -o $P/openxcom.exe "$EXE"
cp -r $R/bin/common $R/bin/standard $P/
cp $R/bin/UFO/README.txt $P/UFO/; cp $R/bin/TFTD/README.txt $P/TFTD/
cp $R/LICENSE.txt $R/CHANGELOG-hires.md $R/README-hires.ko.md $R/README-hires.en.md $P/
cp $R/install/hires/INSTALL.txt $R/install/hires/THIRD_PARTY.txt $R/install/hires/OpenXcom-portable.bat $P/
cp $R/install/hires/user/options.cfg $P/user/
sed -i 's/\r*$/\r/' $P/INSTALL.txt $P/THIRD_PARTY.txt $P/OpenXcom-portable.bat   # Windows line ends
L=$P/licenses
cp $S/SDL-1.2-main/COPYING $L/SDL-1.2_COPYING.txt
cp $S/SDL_mixer-1.2.12/COPYING $L/SDL_mixer_COPYING.txt
cp $S/SDL_image-1.2.12/COPYING $L/SDL_image_COPYING.txt
cp $S/SDL_gfx-2.0.26/LICENSE $L/SDL_gfx_LICENSE.txt
cp $S/freetype-2.13.3/docs/FTL.TXT $L/FreeType_FTL.txt
cp $S/libogg-1.3.5/COPYING $L/libogg_COPYING.txt
cp $S/libvorbis-1.3.7/COPYING $L/libvorbis_COPYING.txt
cp $S/flac-1.4.3/COPYING.Xiph $L/FLAC_COPYING.Xiph.txt
cp $S/libpng-1.6.44/LICENSE $L/libpng_LICENSE.txt
cp $S/zlib-1.3.1/LICENSE $L/zlib_LICENSE.txt
cp /usr/share/doc/mingw-w64-common/copyright $L/mingw-w64_copyright.txt
cp $R/libs/rapidyaml/LICENSE.txt $L/rapidyaml_LICENSE.txt
cp $R/libs/miniz/LICENSE $L/miniz_LICENSE.txt
# never ship fonts, game data or screenshots
if find $P -type f | grep -iE '\.(ttf|otf|ttc|woff2?|fon)$|/(UFO|TFTD)/.+/|\.(cat|pck|tab|spk|scr|lbm|dat)$' | grep -v "/common/\|/standard/" | grep -q .; then
  echo "forbidden files in package"; exit 1; fi
rm -f "$OUT"
(cd $T && python3 - "$OUT" <<'PY'
import zipfile, os, sys
out = sys.argv[1]; n = 0
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk('Extended-8.7.1-hires-1'):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f); z.write(p, p); n += 1
print(n, os.path.getsize(out), zipfile.ZipFile(out).testzip())
PY
)
rm -rf $T
