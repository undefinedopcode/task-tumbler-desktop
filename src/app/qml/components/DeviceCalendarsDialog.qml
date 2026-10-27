import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// The calendars already on this Mac (Calendar.app's accounts), each one a
// switch. The first time, macOS asks whether Task Tumbler may read them.
FormCard.FormCardDialog {
    id: dialog

    property var d: ({})
    readonly property var calendars: d.calendars || []

    function refresh() { d = SyncHub.deviceCalendars() }
    function start() { refresh(); open() }

    title: i18n("Calendars on this Mac")
    standardButtons: Controls.Dialog.Close

    Connections {
        target: SyncHub
        function onDeviceCalendarsChanged() { dialog.refresh() }
    }
    // Back from System Settings: the answer may have changed.
    Connections {
        target: dialog.Controls.ApplicationWindow.window
        function onActiveChanged() { if (dialog.opened && dialog.Controls.ApplicationWindow.window.active) dialog.refresh() }
    }

    // ---- not asked yet ---------------------------------------------------------
    FormCard.FormTextDelegate {
        visible: dialog.d.access === "notAsked"
        text: i18n("Use the calendars you already have")
        description: i18n("Events from iCloud, Google, Exchange or Outlook, and anything else in the Calendar app, show on your schedule and keep tasks out of busy times. They update as soon as they change. Task Tumbler only reads them. macOS will ask you once.")
        textItem.wrapMode: Text.Wrap
    }
    FormCard.FormButtonDelegate {
        visible: dialog.d.access === "notAsked"
        icon.name: "view-calendar"
        text: i18n("Allow access…")
        onClicked: SyncHub.requestCalendarAccess()
    }

    // ---- said no ------------------------------------------------------------------
    FormCard.FormTextDelegate {
        visible: dialog.d.access === "denied"
        text: i18n("Calendar access is off")
        description: i18n("Turn on Task Tumbler in System Settings → Privacy & Security → Calendars, with full access.")
        textItem.wrapMode: Text.Wrap
    }
    FormCard.FormButtonDelegate {
        visible: dialog.d.access === "denied"
        icon.name: "configure"
        text: i18n("Open System Settings")
        onClicked: Qt.openUrlExternally("x-apple.systempreferences:com.apple.preference.security?Privacy_Calendars")
    }

    // ---- the calendars ------------------------------------------------------------
    FormCard.FormTextDelegate {
        visible: dialog.d.access === "granted" && dialog.calendars.length === 0
        text: i18n("No calendars on this Mac yet")
        description: i18n("Add an account in System Settings → Internet Accounts (for Outlook, choose Microsoft Exchange) and it appears here.")
        textItem.wrapMode: Text.Wrap
    }
    Repeater {
        model: dialog.d.access === "granted" ? dialog.calendars : []
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            readonly property bool newAccount: index === 0 || dialog.calendars[index - 1].account !== modelData.account
            Layout.fillWidth: true
            spacing: 0
            FormCard.FormDelegateSeparator { visible: row.index > 0 && !row.newAccount }
            FormCard.FormSectionText {
                visible: row.newAccount
                text: row.modelData.account
            }
            FormCard.FormSwitchDelegate {
                text: row.modelData.title
                checked: row.modelData.feedId !== ""
                leading: Rectangle {
                    implicitWidth: Kirigami.Units.gridUnit * 0.8
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: row.modelData.color
                }
                onToggled: checked ? SyncHub.addDeviceCalendar(row.modelData.id) : SyncHub.removeFeed(row.modelData.feedId)
            }
        }
    }
    FormCard.FormTextDelegate {
        visible: dialog.d.access === "granted" && dialog.calendars.length > 0
        description: i18n("Their events keep tasks out of busy times; ones marked “free” don't.")
        textItem.wrapMode: Text.Wrap
    }
}
