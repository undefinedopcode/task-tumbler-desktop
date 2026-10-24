import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A small rounded label for metadata: due date, estimate, energy, tags.
Rectangle {
    id: chip

    property alias text: label.text
    property string iconName: ""
    property color tint: Kirigami.Theme.disabledTextColor
    property bool strong: false

    implicitWidth: row.implicitWidth + Kirigami.Units.smallSpacing * 3
    implicitHeight: row.implicitHeight + Kirigami.Units.smallSpacing
    radius: height / 2
    color: Qt.alpha(tint, strong ? 0.22 : 0.12)
    border.width: 0

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing / 2

        Kirigami.Icon {
            visible: chip.iconName !== ""
            source: chip.iconName
            Layout.preferredWidth: Kirigami.Units.iconSizes.small - 2
            Layout.preferredHeight: Kirigami.Units.iconSizes.small - 2
            color: chip.strong ? chip.tint : Kirigami.Theme.textColor
            isMask: true
        }
        Controls.Label {
            id: label
            font: Kirigami.Theme.smallFont
            color: chip.strong ? chip.tint : Kirigami.Theme.textColor
        }
    }
}
