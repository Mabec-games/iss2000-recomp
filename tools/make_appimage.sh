#!/bin/sh
# Build the AppImage.
#   tools/make_appimage.sh          portable build: build-deb12/ (compiled in the Debian 12 root via
#                                   tools/debian12.sh), needs glibc >= 2.34 -> Debian 12+, Ubuntu 22.04+
#   tools/make_appimage.sh host     build/ compiled on this machine (needs the host's glibc)
# Output: dist/Mabecs_ISS2000_Widescreen-x86_64.AppImage
# Relies on the host for glibc, libstdc++ (an old bundled copy can break newer GPU drivers), Vulkan drivers,
# X11/Wayland/PulseAudio libraries, D-Bus and xdg-desktop-portal (file dialog).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
APPDIR="$ROOT/dist/AppDir"
OUT="$ROOT/dist/Mabecs_ISS2000_Widescreen-x86_64.AppImage"
if [ "$1" = "host" ]; then
    BIN="$ROOT/build/ISS2000Recompiled"
    LIBSRC=/usr/lib
    LIBS="libSDL2-2.0.so.0 libSDL3.so.0 libstdc++.so.6 libgcc_s.so.1"
else
    BIN="$ROOT/build-deb12/ISS2000Recompiled"
    LIBSRC="$ROOT/ext/debian12/root/usr/lib/x86_64-linux-gnu"
    LIBS="libSDL2-2.0.so.0 libdecor-0.so.0 libsamplerate.so.0 libXss.so.1"
fi
TOOLS="$ROOT/ext/appimage"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/licenses/iss2000recomp"

cp "$BIN" "$APPDIR/usr/bin/ISS2000Recompiled"
strip "$APPDIR/usr/bin/ISS2000Recompiled"

for lib in $LIBS; do
    cp -L "$LIBSRC/$lib" "$APPDIR/usr/lib/$lib"
done
cp "$ROOT/assets/DejaVu-LICENSE.txt" "$APPDIR/usr/share/licenses/iss2000recomp/"

cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/ISS2000Recompiled" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/iss2000recomp.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Mabecs International Superstar Soccer 2000 - Widescreen build
Comment=Native PC recompilation of International Superstar Soccer 2000 (N64)
Exec=ISS2000Recompiled
Icon=iss2000recomp
Categories=Game;SportsGame;
Terminal=false
DESKTOP
cp "$APPDIR/iss2000recomp.desktop" "$APPDIR/usr/share/applications/"
cp "$ROOT/assets/iss2000recomp.png" "$APPDIR/iss2000recomp.png"
cp "$ROOT/assets/iss2000recomp.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
ln -sf iss2000recomp.png "$APPDIR/.DirIcon"

mkdir -p "$ROOT/dist"
# Build to a temporary name and rename over the old file, so it works even while the old AppImage is running.
ARCH=x86_64 "$TOOLS/appimagetool-x86_64.AppImage" --runtime-file "$TOOLS/runtime-x86_64" "$APPDIR" "$OUT.new"
mv -f "$OUT.new" "$OUT"
rm -rf "$APPDIR"
ls -la "$OUT"
