#!/bin/sh
# Build ~/Applications/LMMS-Nebula.app: the stock LMMS 1.3.0-alpha.2 bundle (arm64, from LMMS's
# GitHub releases, installed at ~/Applications/LMMS.app) with its `lmms` binary replaced by one
# built from this branch, which adds the control socket. The stock LMMS.app is left as it is.
#
#   nebula/build-macos-app.sh                    # build the binary (4 jobs, niced) and the bundle
#
# Only the `lmms` target is compiled (about 270 files), not the plugins: the stock bundle's
# plugins resolve LMMS's symbols from whichever `lmms` loads them, so they work with ours as long
# as it's the same source, Qt (Homebrew qt@5 5.15.19) and features (the Brewfile's libraries).
# Setup, once: brew install qt@5 fftw libsamplerate lilv lv2 jack stk ccache, then
#   git submodule update --init --depth 1 src/3rdparty/ringbuffer src/3rdparty/hiir/hiir \
#       src/3rdparty/weakjack/weakjack
set -eu
SRC=$(cd "$(dirname "$0")/.." && pwd)
STOCK="$HOME/Applications/LMMS.app"
APP="$HOME/Applications/LMMS-Nebula.app"
QT=/opt/homebrew/opt/qt@5
export PATH="$QT/bin:$PATH"

if [ ! -f "$SRC/build/build.ninja" ]; then
    (cd "$SRC" && . .github/workflows/macos-env.sh brew qt 5 >/dev/null &&
        cmake -S . -B build -G Ninja -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release \
            -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DCMAKE_PREFIX_PATH="$QT;/opt/homebrew" \
            -DCMAKE_IGNORE_PREFIX_PATH=/usr/local -DCMAKE_SYSTEM_IGNORE_PREFIX_PATH=/usr/local >/dev/null)
    # (/usr/local can hold an old Intel Homebrew, whose x86_64 libraries won't link)
fi
nice -n 10 ninja -C "$SRC/build" -j4 lmms

echo "== bundle"
rsync -a --delete "$STOCK/" "$APP/"
BIN="$APP/Contents/MacOS/lmms"
cp "$SRC/build/lmms" "$BIN"
chmod u+w "$BIN"

# Point every Homebrew library at the bundle's copy, as the stock binary does
otool -L "$BIN" | awk 'NR > 1 { print $1 }' | grep '^/opt/homebrew' | while read -r dep; do
    case "$dep" in
        *.framework/*) name="${dep#*/lib/}" ;;      # QtCore.framework/Versions/5/QtCore
        *) name=$(basename "$dep") ;;
    esac
    [ -e "$APP/Contents/Frameworks/$name" ] || { echo "the bundle has no $name" >&2; exit 1; }
    install_name_tool -change "$dep" "@loader_path/../Frameworks/$name" "$BIN" 2>/dev/null
done
otool -l "$BIN" | awk '/LC_RPATH/ { getline; getline; print $2 }' | grep '^/opt/homebrew' | while read -r rp; do
    install_name_tool -delete_rpath "$rp" "$BIN" 2>/dev/null
done
if otool -L "$BIN" | grep -q /opt/homebrew; then
    echo "still linked to Homebrew:" >&2; otool -L "$BIN" | grep /opt/homebrew >&2; exit 1
fi

# Its own settings (seeded from the stock LMMS's once) and the control socket, via a launcher.
# Its working folder is its own too: the stock one is in ~/Documents, which macOS guards with a
# permission prompt for every new app, and LMMS scans it while starting, before any window shows.
SUPPORT="$HOME/Library/Application Support/LMMS-Nebula"
mkdir -p "$SUPPORT/workspace"
if [ ! -f "$SUPPORT/lmmsrc.xml" ] && [ -f "$HOME/.lmmsrc.xml" ]; then
    sed -e "s|$HOME/Documents/lmms/|$SUPPORT/workspace/|g" -e "s|/LMMS.app/|/LMMS-Nebula.app/|g" \
        "$HOME/.lmmsrc.xml" > "$SUPPORT/lmmsrc.xml"
fi
cat > "$APP/Contents/MacOS/lmms-nebula" <<'EOF'
#!/bin/sh
# LMMS-Nebula: LMMS with the control socket on, and settings of its own (not ~/.lmmsrc.xml)
SUPPORT="$HOME/Library/Application Support/LMMS-Nebula"
mkdir -p "$SUPPORT"
export LMMS_CONTROL_SOCKET="${LMMS_CONTROL_SOCKET:-$SUPPORT/control.sock}"
exec "$(dirname "$0")/lmms" --config "$SUPPORT/lmmsrc.xml" "$@"
EOF
chmod +x "$APP/Contents/MacOS/lmms-nebula"

PLIST="$APP/Contents/Info.plist"
plutil -replace CFBundleExecutable -string lmms-nebula "$PLIST"
plutil -replace CFBundleIdentifier -string io.lmms.LMMS-Nebula "$PLIST"
plutil -replace CFBundleName -string LMMS-Nebula "$PLIST"
plutil -replace CFBundleDisplayName -string LMMS-Nebula "$PLIST" 2>/dev/null || plutil -insert CFBundleDisplayName -string LMMS-Nebula "$PLIST"
# Leave .mmp and .mmpz opening in the stock LMMS: this one opens them only when asked to
plutil -remove CFBundleDocumentTypes "$PLIST" 2>/dev/null || true

codesign --force --deep --sign - "$APP" 2>&1 | grep -v "replacing existing signature" || true
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP"
echo "$APP: built from $(git -C "$SRC" rev-parse --short HEAD) ($(git -C "$SRC" rev-parse --abbrev-ref HEAD))"
