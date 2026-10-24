import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// Create or edit a category: its name, colour and the hours on each weekday
// when its tasks may be scheduled.
Kirigami.Dialog {
    id: dialog

    property var category: ({})
    property var days: []
    readonly property var swatches: ["#6366f1", "#0ea5e9", "#10b981", "#84cc16", "#f59e0b", "#f97316", "#ef4444", "#ec4899", "#a855f7", "#64748b"]

    function edit(c) {
        category = c ? JSON.parse(JSON.stringify(c)) : { name: "", color: swatches[0], isDefault: false, showOnSchedule: true }
        // Monday-first for display; stored Sunday-first.
        const src = c ? c.days : [0, 1, 2, 3, 4, 5, 6].map(d => ({ day: d, enabled: d >= 1 && d <= 5, start: "09:00", end: "17:00" }))
        days = [1, 2, 3, 4, 5, 6, 0].map(d => Object.assign({}, src.find(x => x.day === d)))
        nameField.text = category.name
        open()
        nameField.forceActiveFocus()
    }

    title: category.id ? i18n("Edit category") : i18n("New category")
    preferredWidth: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Cancel

    onAccepted: {
        const c = Object.assign({}, category, { name: nameField.text, days: days })
        AppController.saveCategory(c)
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true
            Controls.TextField {
                id: nameField
                Kirigami.FormData.label: i18n("Name:")
                placeholderText: i18n("Work, Home, Side project…")
            }
            Flow {
                Kirigami.FormData.label: i18n("Colour:")
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: dialog.swatches
                    Rectangle {
                        required property string modelData
                        width: Kirigami.Units.gridUnit * 1.4
                        height: width
                        radius: width / 2
                        color: modelData
                        border.width: dialog.category.color === modelData ? 3 : 0
                        border.color: Kirigami.Theme.textColor
                        TapHandler { onTapped: dialog.category = Object.assign({}, dialog.category, { color: parent.modelData }) }
                    }
                }
            }
            Controls.CheckBox {
                Kirigami.FormData.label: i18n("Options:")
                text: i18n("Default for new tasks")
                checked: !!dialog.category.isDefault
                enabled: !dialog.category.isDefault || !dialog.category.id
                onToggled: dialog.category = Object.assign({}, dialog.category, { isDefault: checked })
            }
            Controls.CheckBox {
                text: i18n("Shade its hours on the schedule")
                checked: !!dialog.category.showOnSchedule
                onToggled: dialog.category = Object.assign({}, dialog.category, { showOnSchedule: checked })
            }
        }

        Kirigami.Heading {
            level: 5
            text: i18n("When its tasks can be scheduled")
        }
        Repeater {
            model: dialog.days.length
            RowLayout {
                required property int index
                readonly property var d: dialog.days[index]
                function set(field, value) {
                    const copy = dialog.days.slice()
                    copy[index] = Object.assign({}, copy[index], { [field]: value })
                    dialog.days = copy
                }
                Controls.CheckBox {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    text: Qt.locale().dayName(parent.d.day, Locale.ShortFormat)
                    checked: parent.d.enabled
                    onToggled: parent.set("enabled", checked)
                }
                Controls.TextField {
                    enabled: parent.d.enabled
                    text: parent.d.start
                    inputMask: "99:99"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                    onEditingFinished: parent.set("start", text)
                }
                Controls.Label { text: "–" }
                Controls.TextField {
                    enabled: parent.d.enabled
                    text: parent.d.end
                    inputMask: "99:99"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                    onEditingFinished: parent.set("end", text)
                }
            }
        }
    }
}
