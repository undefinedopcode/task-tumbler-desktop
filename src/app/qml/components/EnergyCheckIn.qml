import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// One tap to say how much energy there is. Five words rather than a 1-10
// slider: easier to answer honestly when tired.
Kirigami.AbstractCard {
    id: card

    signal checkedIn(int level)

    Layout.fillWidth: true

    readonly property var choices: [
        { level: 2, label: i18n("Wiped") },
        { level: 4, label: i18n("Low") },
        { level: 6, label: i18n("OK") },
        { level: 8, label: i18n("Good") },
        { level: 10, label: i18n("Great") }
    ]

    // Side by side when there's room, stacked in narrow cards.
    readonly property bool narrow: width < Kirigami.Units.gridUnit * 38

    contentItem: GridLayout {
        columns: card.narrow ? 1 : 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon {
                source: "battery-060"
                implicitWidth: Kirigami.Units.iconSizes.medium
                implicitHeight: implicitWidth
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Controls.Label {
                    text: i18n("How's your energy right now?")
                    font.weight: Font.DemiBold
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18n("Plans fit around it: low energy keeps demanding work for another day.")
                    color: Kirigami.Theme.disabledTextColor
                    font: Kirigami.Theme.smallFont
                }
            }
        }
        Flow {
            Layout.fillWidth: card.narrow
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                model: card.choices
                Controls.Button {
                    required property var modelData
                    text: modelData.label
                    onClicked: {
                        AppController.logEnergy(modelData.level)
                        card.checkedIn(modelData.level)
                    }
                }
            }
        }
    }
}
