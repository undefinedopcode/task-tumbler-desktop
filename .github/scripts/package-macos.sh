#!/usr/bin/env bash
# Turns the built task-tumbler.app into a self-contained "Task Tumbler.app"
# (Qt with QtWebEngine, and the KDE libraries inside), signs it ad hoc and
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

# Qt (QtWebEngine with its helper app and resources included) and the
# libraries from the KDE prefix and Homebrew.
"$QT/bin/macdeployqt" "$APP" \
    -libpath="$KDE/lib" \
    -libpath="$(brew --prefix)/lib" \
    -verbose=1

# macdeployqt misses some paths: Homebrew's read-only dylibs, and the
# WebEngine helper, whose Qt frameworks it links by absolute Homebrew path
# (some of them, like QtQuick, the app itself doesn't use). Copy in what's
# missing and point everything at @rpath: "@executable_path/../Frameworks"
# is wrong inside the helper, which is an executable of its own.
FRAMEWORKS="$APP/Contents/Frameworks"
MAIN="$APP/Contents/MacOS/task-tumbler"
HELPER="$FRAMEWORKS/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"
outside="^($(brew --prefix)|$KDE)/"
in_bundle='^@executable_path/\.\./Frameworks/(.+)$'
framework_re='^(.*/)?([^/]+\.framework)/(.+)$'

machos() {
    find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 | while IFS= read -r -d '' f; do
        file "$f" | grep -q 'Mach-O' && printf '%s\0' "$f"
    done
}

# The @rpath name for a dependency outside the bundle, copying it in first
# if it isn't there yet. Prints nothing if it can't be found.
bundled() {
    local dep="$1"
    if [[ "$dep" =~ $framework_re ]]; then
        local dir="${BASH_REMATCH[1]}" fw="${BASH_REMATCH[2]}" rest="${BASH_REMATCH[3]}"
        if [ ! -e "$FRAMEWORKS/$fw" ]; then
            [ -d "$dir$fw" ] || return 0
            ditto "$dir$fw" "$FRAMEWORKS/$fw"
            find "$FRAMEWORKS/$fw" -name Headers -prune -exec rm -rf {} +
            chmod -R u+w "$FRAMEWORKS/$fw"
        fi
        echo "@rpath/$fw/$rest"
    else
        local lib
        lib="$(basename "$dep")"
        if [ ! -e "$FRAMEWORKS/$lib" ]; then
            [ -f "$dep" ] || return 0
            cp "$dep" "$FRAMEWORKS/$lib"
            chmod u+w "$FRAMEWORKS/$lib"
        fi
        echo "@rpath/$lib"
    fi
}

relink() {
    local bin="$1" id dep to
    chmod u+w "$bin"
    id="$(otool -D "$bin" | tail -n +2)"
    if [[ "$id" =~ $outside ]]; then
        to="$(bundled "$id")"
        [ -n "$to" ] && install_name_tool -id "$to" "$bin" 2>/dev/null
    elif [[ "$id" =~ $in_bundle ]]; then
        install_name_tool -id "@rpath/${BASH_REMATCH[1]}" "$bin" 2>/dev/null
    fi
    otool -L "$bin" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        to=""
        if [[ "$dep" =~ $outside ]]; then
            to="$(bundled "$dep")"
            [ -n "$to" ] || echo "warning: $bin needs $dep, which isn't on this machine" >&2
        elif [[ "$dep" =~ $in_bundle ]]; then
            to="@rpath/${BASH_REMATCH[1]}"
        fi
        [ -n "$to" ] && install_name_tool -change "$dep" "$to" "$bin" 2>/dev/null
        true
    done
}

# Copied-in frameworks have dependencies of their own: repeat until a pass
# brings in nothing new.
while :; do
    before="$(find "$FRAMEWORKS" -maxdepth 1 | wc -l)"
    machos | while IFS= read -r -d '' bin; do relink "$bin"; done
    after="$(find "$FRAMEWORKS" -maxdepth 1 | wc -l)"
    [ "$before" = "$after" ] && break
done

add_rpath() {
    otool -l "$1" | grep -A2 LC_RPATH | grep -qF "path $2 " || install_name_tool -add_rpath "$2" "$1"
}
add_rpath "$MAIN" "@executable_path/../Frameworks"
# Helper: .../Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS
add_rpath "$HELPER" "@executable_path/../../../../../../.."

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
