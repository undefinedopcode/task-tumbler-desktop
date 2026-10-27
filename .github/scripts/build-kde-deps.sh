#!/usr/bin/env bash
# Builds the KDE libraries Task Tumbler needs on macOS into $1 (default
# ~/kde-deps), against Homebrew's Qt. Homebrew doesn't package KDE
# Frameworks; this is the handful we use, nothing else.
set -euo pipefail

PREFIX="${1:-$HOME/kde-deps}"
KF_VERSION="${KF_VERSION:-v6.31.0}"
ADDONS_VERSION="${ADDONS_VERSION:-v1.15.0}"
QT_PREFIX="$(brew --prefix qt)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

build() { # repo-path tag [cmake args...]
    local repo="$1" tag="$2"
    shift 2
    local name
    name="$(basename "$repo")"
    echo "::group::$name $tag"
    git clone --quiet --depth 1 --branch "$tag" "https://invent.kde.org/$repo.git" "$WORK/$name"
    cmake -S "$WORK/$name" -B "$WORK/$name/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX;$PREFIX;$(brew --prefix)" \
        -DBUILD_TESTING=OFF -DBUILD_QCH=OFF -DBUILD_EXAMPLES=OFF \
        "$@"
    cmake --build "$WORK/$name/build"
    cmake --install "$WORK/$name/build"
    echo "::endgroup::"
}

# In dependency order.
build frameworks/extra-cmake-modules "$KF_VERSION"
build frameworks/kcoreaddons "$KF_VERSION"
build frameworks/ki18n "$KF_VERSION"
build frameworks/kconfig "$KF_VERSION"
build frameworks/kwindowsystem "$KF_VERSION"
build frameworks/knotifications "$KF_VERSION"
build frameworks/kstatusnotifieritem "$KF_VERSION"
build frameworks/kirigami "$KF_VERSION"
build frameworks/kguiaddons "$KF_VERSION" -DWITH_WAYLAND=OFF -DWITH_X11=OFF
build frameworks/kcolorscheme "$KF_VERSION"
build frameworks/kcrash "$KF_VERSION"
build frameworks/karchive "$KF_VERSION"
build frameworks/kwidgetsaddons "$KF_VERSION"
# The icons go into a library, not loose files; KIconThemes links it in and
# the app picks the theme up with KIconTheme::initTheme().
build frameworks/breeze-icons "$KF_VERSION" -DWITH_ICON_GENERATION=OFF -DSKIP_INSTALL_ICONS=ON
build frameworks/kiconthemes "$KF_VERSION"
build libraries/kirigami-addons "$ADDONS_VERSION"
build frameworks/kcalendarcore "$KF_VERSION"
build frameworks/prison "$KF_VERSION"
