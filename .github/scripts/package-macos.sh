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

# QML modules the app imports, found by scanning its QML; libraries from Qt,
# the KDE prefix and Homebrew.
"$QT/bin/macdeployqt" "$APP" \
    -qmldir="$SRC/src/app/qml" \
    -qmlimport="$KDE/lib/qml" \
    -libpath="$KDE/lib" \
    -libpath="$(brew --prefix)/lib" \
    -verbose=1

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
