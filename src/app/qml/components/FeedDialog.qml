import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// Subscribe to a calendar by its .ics address. The address is checked
// before anything is saved.
FormCard.FormCardDialog {
    id: feedDialog

    readonly property var swatches: ["#64748b", "#6366f1", "#0ea5e9", "#10b981", "#f59e0b", "#ef4444", "#ec4899", "#a855f7"]
    property string color: swatches[0]
    property string blocking: "block_all"
    property bool busy: false
    property string error: ""

    function start() {
        nameField.text = ""
        urlField.text = ""
        color = swatches[0]
        blocking = "block_all"
        error = ""
        busy = false
        open()
        urlField.forceActiveFocus()
    }

    title: i18n("Subscribe to a calendar")
    standardButtons: Controls.Dialog.Cancel

    function subscribe() {
        busy = true
        error = ""
        SyncHub.addFeed({ name: nameField.text.trim(), url: urlField.text.trim(), color: color, blocking: blocking })
    }

    Connections {
        target: SyncHub
        function onFeedAdded() { if (feedDialog.busy) { feedDialog.busy = false; feedDialog.close() } }
        function onFeedFailed(error) { feedDialog.busy = false; feedDialog.error = error }
    }

    FormCard.FormTextFieldDelegate {
        id: urlField
        label: i18n("Address")
        placeholderText: "https://… .ics, webcal://… or a file path"
        onAccepted: if (text.trim() !== "" && !feedDialog.busy) feedDialog.subscribe()
    }
    FormCard.FormTextDelegate {
        text: i18n("Where to find it")
        description: i18n("Google Calendar: Settings → your calendar → “Secret address in iCal format”. Nextcloud: the calendar's ⋯ menu → “Copy subscription link”. Outlook: Settings → Shared calendars → Publish → ICS.")
    }
    FormCard.FormDelegateSeparator {}
    FormCard.FormTextFieldDelegate {
        id: nameField
        label: i18n("Name (optional)")
        placeholderText: i18n("Work, Family, School holidays…")
    }
    FormCard.FormComboBoxDelegate {
        text: i18n("While its events are on")
        model: [
            { value: "block_all", text: i18n("Nothing else gets scheduled") },
            { value: "recurring_only", text: i18n("Only routines") },
            { value: "none", text: i18n("Anything can be scheduled") }
        ]
        textRole: "text"
        valueRole: "value"
        currentIndex: Math.max(0, model.findIndex(m => m.value === feedDialog.blocking))
        onActivated: feedDialog.blocking = currentValue
    }
    FormCard.AbstractFormDelegate {
        background: null
        contentItem: Flow {
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                model: feedDialog.swatches
                Rectangle {
                    required property string modelData
                    width: Kirigami.Units.gridUnit * 1.3
                    height: width
                    radius: width / 2
                    color: modelData
                    border.width: feedDialog.color === modelData ? 3 : 0
                    border.color: Kirigami.Theme.textColor
                    TapHandler { onTapped: feedDialog.color = parent.modelData }
                }
            }
        }
    }
    FormCard.FormButtonDelegate {
        // Not a dialog button: the dialog stays open until the address checks out.
        text: feedDialog.busy ? i18n("Checking…") : i18n("Subscribe")
        icon.name: "list-add"
        enabled: !feedDialog.busy && urlField.text.trim() !== ""
        onClicked: feedDialog.subscribe()
    }
    FormCard.FormTextDelegate {
        visible: feedDialog.error !== ""
        text: feedDialog.error
        textItem.color: Kirigami.Theme.negativeTextColor
        textItem.wrapMode: Text.Wrap
    }
}
