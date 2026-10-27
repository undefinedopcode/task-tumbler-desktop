#!/usr/bin/env bash
# Turns the built task-tumbler.app into a self-contained "Task Tumbler.app"
# (Qt, the KDE libraries and their QML modules inside), signs it ad hoc and
# zips it.
#   package-macos.sh <build dir> <kde prefix> <out dir>
set -euo pipefail

BUILD="$1"
KDE="$2"
OUT="$3"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
QT="$(brew --prefix qt)"

mkdir -p "$OUT"
APP="$OUT/Task Tumbler.app"
rm -rf "$APP"
cp -R "$BUILD/src/app/task-tumbler.app" "$APP"

# KIconThemes' icon engine, which recolours the Breeze icons for light or dark.
# KIconTheme::initTheme() looks for it in <plugins>/kiconthemes6.
ENGINE="$(find "$KDE/lib" -name 'KIconEnginePlugin.*' -path '*iconengines*' | head -1)"
if [ -z "$ENGINE" ]; then
    echo "KIconThemes' icon engine plugin isn't in $KDE" >&2
    exit 1
fi
mkdir -p "$APP/Contents/PlugIns/kiconthemes6/iconengines"
cp "$ENGINE" "$APP/Contents/PlugIns/kiconthemes6/iconengines/"
ENGINE_IN_APP="$APP/Contents/PlugIns/kiconthemes6/iconengines/$(basename "$ENGINE")"

# QML modules the app imports, found by scanning its QML; libraries from Qt,
# the KDE prefix and Homebrew, for the app and the icon engine.
"$QT/bin/macdeployqt" "$APP" \
    -executable="$ENGINE_IN_APP" \
    -qmldir="$SRC/src/app/qml" \
    -qmlimport="$KDE/lib/qml" \
    -libpath="$KDE/lib" \
    -libpath="$(brew --prefix)/lib" \
    -verbose=1

# Mac colours for Kirigami: it reads styles/<style>/Theme.qml from beside its
# own QML, and has none for ours (see the file).
THEME_DIR="$APP/Contents/Resources/qml/org/kde/kirigami/styles/org.kde.tasktumbler.macstyle"
mkdir -p "$THEME_DIR"
cp "$SRC/resources/macos/kirigami/Theme.qml" "$THEME_DIR/Theme.qml"

# macdeployqt misses some paths, e.g. in Homebrew's read-only dylibs: point
# anything still aimed at Homebrew or the KDE prefix at the bundled copy.
FRAMEWORKS="$APP/Contents/Frameworks"
outside="^($(brew --prefix)|$KDE)/"
find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 | while IFS= read -r -d '' bin; do
    file "$bin" | grep -q 'Mach-O' || continue
    chmod u+w "$bin"
    id="$(otool -D "$bin" | tail -n +2)"
    if [[ "$id" =~ $outside ]]; then
        install_name_tool -id "@executable_path/../Frameworks/$(basename "$id")" "$bin" 2>/dev/null
    fi
    otool -L "$bin" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        [[ "$dep" =~ $outside ]] || continue
        if [ -e "$FRAMEWORKS/$(basename "$dep")" ]; then
            install_name_tool -change "$dep" "@executable_path/../Frameworks/$(basename "$dep")" "$bin" 2>/dev/null
        else
            echo "warning: $bin needs $dep, which isn't bundled" >&2
        fi
    done
done

# Nothing may still point outside the bundle: on another Mac those paths don't exist.
leaks="$(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 \
    | xargs -0 otool -L 2>/dev/null | grep -E "$(brew --prefix)|$KDE" | grep -v ':$' || true)"
if [ -n "$leaks" ]; then
    echo "These still link outside the bundle:" >&2
    echo "$leaks" >&2
    exit 1
fi

# Ad hoc: enough to run on the machine it's copied to (after the first-open
# warning), not for handing out widely.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

ditto -c -k --keepParent "$APP" "$OUT/Task-Tumbler-macOS.zip"
echo "Packaged $(du -sh "$APP" | cut -f1) app: $OUT/Task-Tumbler-macOS.zip"
