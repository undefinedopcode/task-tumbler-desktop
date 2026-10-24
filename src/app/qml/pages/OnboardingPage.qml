import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// First run: bring tasks over from the web app, or start with a clean slate.
Kirigami.Page {
    id: page

    property string report: ""
    readonly property bool imported: report !== "" && !report.startsWith(i18n("Import failed")) && !report.startsWith(i18n("Couldn't"))

    title: i18n("Welcome")

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 34)
        spacing: Kirigami.Units.largeSpacing * 2

        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/icons/org.kde.tasktumbler.svg"
            fallback: "view-task"
            implicitWidth: Kirigami.Units.iconSizes.enormous
            implicitHeight: implicitWidth
        }
        Kirigami.Heading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: i18n("Task Tumbler")
        }
        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: i18n("Everything stays on this computer. No account, no sign-in.")
        }

        RowLayout {
            visible: !page.imported
            Layout.alignment: Qt.AlignHCenter
            spacing: Kirigami.Units.largeSpacing
            Controls.Button {
                text: i18n("Import from the web app…")
                icon.name: "document-import"
                onClicked: importDialog.open()
            }
            Controls.Button {
                text: i18n("Start fresh")
                icon.name: "go-next"
                onClicked: AppController.startFresh()
            }
        }
        Controls.Label {
            visible: !page.imported && page.report === ""
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("To import, run “npm run export-desktop -- --out tumbler.json” in the web app's server folder first.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.report !== ""
            type: page.imported ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
            text: page.report
            actions: page.imported ? [continueAction] : []
        }
        Kirigami.Action {
            id: continueAction
            text: i18n("Go to Today")
            icon.name: "go-next"
            onTriggered: applicationWindow().showPage("today")
        }
    }

    FileDialog {
        id: importDialog
        title: i18n("Choose a Task Tumbler export")
        nameFilters: [i18n("Task Tumbler export (*.json)")]
        onAccepted: page.report = AppController.importExport(selectedFile)
    }
}
