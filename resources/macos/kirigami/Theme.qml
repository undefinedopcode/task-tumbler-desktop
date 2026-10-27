/*
 * Kirigami's colours on macOS. Kirigami looks for styles/<Qt Quick Controls
 * style>/Theme.qml beside its QML files, and ships none for "macOS", so it
 * falls back to Breeze's. package-macos.sh puts this one there instead: the
 * system's own palette (light or dark, following the system) with the
 * user's accent colour, and Apple's system colours for good, bad and
 * attention.
 */

import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.BasicThemeDefinition {
    id: theme

    readonly property bool dark: palette.window.hslLightness < 0.5
    // Apple's systemRed, systemOrange, systemGreen and link blue, light / dark.
    readonly property color red: dark ? "#ff453a" : "#ff3b30"
    readonly property color orange: dark ? "#ff9f0a" : "#ff9500"
    readonly property color green: dark ? "#30d158" : "#28cd41"
    readonly property color accent: palette.accent
    readonly property color selectedText: "#ffffff"
    readonly property color hover: Qt.alpha(palette.windowText, dark ? 0.10 : 0.06)

    textColor: palette.windowText
    disabledTextColor: disabledPalette.windowText

    highlightColor: accent
    highlightedTextColor: selectedText
    backgroundColor: palette.window
    alternateBackgroundColor: Qt.darker(palette.window, dark ? 0.9 : 1.04)
    activeTextColor: accent
    activeBackgroundColor: accent
    linkColor: accent
    linkBackgroundColor: accent
    visitedLinkColor: Qt.darker(accent, 1.3)
    visitedLinkBackgroundColor: Qt.darker(accent, 1.3)
    hoverColor: accent
    focusColor: accent
    negativeTextColor: red
    negativeBackgroundColor: Qt.alpha(red, 0.2)
    neutralTextColor: orange
    neutralBackgroundColor: Qt.alpha(orange, 0.2)
    positiveTextColor: green
    positiveBackgroundColor: Qt.alpha(green, 0.2)

    buttonTextColor: palette.buttonText
    buttonBackgroundColor: palette.button
    buttonAlternateBackgroundColor: Qt.darker(palette.button, dark ? 0.9 : 1.04)
    buttonHoverColor: accent
    buttonFocusColor: accent

    // Lists and cards: the content background (white, or near black).
    viewTextColor: palette.text
    viewBackgroundColor: palette.base
    viewAlternateBackgroundColor: palette.alternateBase
    viewHoverColor: hover
    viewFocusColor: accent

    selectionTextColor: selectedText
    selectionBackgroundColor: accent
    selectionAlternateBackgroundColor: Qt.darker(accent, 1.1)
    selectionHoverColor: Qt.lighter(accent, 1.1)
    selectionFocusColor: accent

    tooltipTextColor: palette.text
    tooltipBackgroundColor: palette.base
    tooltipAlternateBackgroundColor: palette.alternateBase
    tooltipHoverColor: accent
    tooltipFocusColor: accent

    // Always dark, as on Plasma.
    complementaryTextColor: "#ffffff"
    complementaryBackgroundColor: "#1e1e1e"
    complementaryAlternateBackgroundColor: "#2a2a2a"
    complementaryHoverColor: accent
    complementaryFocusColor: accent

    // Title and tool bars: the window colour, as in Mac apps.
    headerTextColor: palette.windowText
    headerBackgroundColor: palette.window
    headerAlternateBackgroundColor: palette.window
    headerHoverColor: hover
    headerFocusColor: accent

    defaultFont: fontMetrics.font

    property list<QtObject> children: [
        TextMetrics {
            id: fontMetrics
        },
        SystemPalette {
            id: palette
            colorGroup: SystemPalette.Active
        },
        SystemPalette {
            id: disabledPalette
            colorGroup: SystemPalette.Disabled
        }
    ]

    function __propagateColorSet(object, context) {}
    function __propagateTextColor(object, color) {}
    function __propagateBackgroundColor(object, color) {}
    function __propagatePrimaryColor(object, color) {}
    function __propagateAccentColor(object, color) {}
}
