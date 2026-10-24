import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// Energy check-ins and last night's sleep, with two weeks of history.
FormCard.FormCardPage {
    id: page

    property var history: AppController.energyHistory(14)
    property var health: AppController.healthToday()

    title: i18n("Energy")

    Connections {
        target: AppController
        function onChanged() {
            page.history = AppController.energyHistory(14)
            page.health = AppController.healthToday()
        }
    }

    FormCard.FormHeader { title: i18n("Right now") }
    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                Controls.Label {
                    text: AppController.energy.level !== undefined
                          ? i18n("Last check-in: %1 at %2", AppController.energy.label, Format.time(AppController.energy.at))
                          : i18n("No check-in yet today")
                    color: Kirigami.Theme.disabledTextColor
                }
                EnergyCheckIn {
                    background: null
                }
            }
        }
    }

    FormCard.FormHeader { title: i18n("Last night") }
    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            id: sleep
            label: i18n("Sleep score (0-100, from your watch, optional)")
            text: page.health.sleep !== undefined && page.health.sleep !== null ? page.health.sleep : ""
            validator: IntValidator { bottom: 0; top: 100 }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: readiness
            label: i18n("Readiness score (0-100, optional)")
            text: page.health.readiness !== undefined && page.health.readiness !== null ? page.health.readiness : ""
            validator: IntValidator { bottom: 0; top: 100 }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: hours
            label: i18n("Hours slept (optional)")
            text: page.health.hours !== undefined && page.health.hours !== null ? page.health.hours : ""
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            text: i18n("Save")
            icon.name: "document-save"
            onClicked: AppController.saveHealth({ sleep: sleep.text, readiness: readiness.text, hours: hours.text })
        }
    }

    FormCard.FormHeader { title: i18n("The last two weeks") }
    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Bars: average energy per day (1-10). Dots: sleep score (0-100).
                Item {
                    id: chart
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 9
                    readonly property var days: page.history.days || []
                    readonly property real slot: width / Math.max(1, days.length)

                    Controls.Label {
                        anchors.centerIn: parent
                        width: parent.width * 0.8
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: !chart.days.some(d => (d.energy !== undefined && d.energy !== null) || (d.sleep !== undefined && d.sleep !== null))
                        text: i18n("Check in a few times a day and this fills in, so you can spot patterns.")
                        color: Kirigami.Theme.disabledTextColor
                    }
                    Repeater {
                        model: [0.25, 0.5, 0.75, 1.0]
                        Kirigami.Separator {
                            required property real modelData
                            width: chart.width
                            y: chart.height * (1 - modelData)
                            opacity: 0.4
                        }
                    }
                    Repeater {
                        model: chart.days
                        Item {
                            required property var modelData
                            required property int index
                            x: index * chart.slot
                            width: chart.slot
                            height: chart.height
                            Rectangle {
                                visible: parent.modelData.energy !== undefined && parent.modelData.energy !== null
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                width: parent.width * 0.55
                                height: visible ? chart.height * parent.modelData.energy / 10 : 0
                                radius: Kirigami.Units.cornerRadius
                                color: parent.modelData.energy <= 4 ? Format.priorityColors.high : Kirigami.Theme.positiveTextColor
                                opacity: 0.75
                            }
                            Rectangle {
                                visible: parent.modelData.sleep !== undefined && parent.modelData.sleep !== null
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: visible ? chart.height * (1 - parent.modelData.sleep / 100) - height / 2 : 0
                                width: Kirigami.Units.smallSpacing * 2.5
                                height: width
                                radius: width / 2
                                color: Format.priorityColors.medium
                                border.color: Kirigami.Theme.backgroundColor
                                border.width: 2
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: chart.days
                        Controls.Label {
                            required property var modelData
                            required property int index
                            Layout.preferredWidth: chart.slot
                            horizontalAlignment: Text.AlignHCenter
                            text: index % 2 === (chart.days.length - 1) % 2 ? modelData.label : ""
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }
                RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    Chip { text: i18n("Energy (average of the day's check-ins)"); tint: Kirigami.Theme.positiveTextColor; strong: true }
                    Chip { text: i18n("Sleep score"); tint: Format.priorityColors.medium; strong: true }
                }
            }
        }
    }

    FormCard.FormHeader {
        visible: (page.history.energy || []).length > 0
        title: i18n("Check-ins")
    }
    FormCard.FormCard {
        visible: (page.history.energy || []).length > 0
        Repeater {
            model: (page.history.energy || []).slice().reverse().slice(0, 12)
            FormCard.FormTextDelegate {
                required property var modelData
                text: modelData.label + " (" + modelData.level + "/10)"
                description: Format.dueLabel(Format.isoDate(new Date(modelData.at))) + " " + Format.time(modelData.at)
            }
        }
    }
}
