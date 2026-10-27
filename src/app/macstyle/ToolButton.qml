// The Mac look for tool buttons. Qt's macOS style has no ToolButton of its
// own and falls back to Basic's, a grey square whether flat or not; this is
// the borderless kind Mac apps use, with a soft highlight under the pointer,
// and icons in the text colour so they follow dark mode.

import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    spacing: 6
    hoverEnabled: true

    icon.width: 16
    icon.height: 16
    icon.color: control.enabled ? control.palette.buttonText : control.palette.placeholderText

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.icon.color
    }

    background: Rectangle {
        implicitWidth: 28
        implicitHeight: 28
        radius: 6
        color: control.down || control.checked ? Qt.alpha(control.palette.buttonText, 0.16)
             : control.hovered && control.enabled ? Qt.alpha(control.palette.buttonText, 0.08)
             : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: control.palette.accent
    }
}
