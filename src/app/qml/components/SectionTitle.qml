import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

RowLayout {
    property alias text: heading.text
    property int count: -1
    property color accent: Kirigami.Theme.textColor

    Layout.fillWidth: true
    Layout.topMargin: Kirigami.Units.largeSpacing
    spacing: Kirigami.Units.smallSpacing

    Kirigami.Heading {
        id: heading
        level: 4
        color: parent.accent
    }
    Controls.Label {
        visible: parent.count >= 0
        text: parent.count
        color: Kirigami.Theme.disabledTextColor
    }
    Item { Layout.fillWidth: true }
}
