import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// Add something to open when a task starts.
Kirigami.Dialog {
    id: launcherDialog

    property string taskId: ""
    property string kind: "url"
    property var apps: []
    property string appFilter: ""
    property string chosenApp: ""

    function openFor(id) {
        taskId = id
        kind = "url"
        urlField.text = ""
        fileField.text = ""
        commandField.text = ""
        chosenApp = ""
        appFilter = ""
        onStart.checked = true
        if (apps.length === 0)
            apps = AppController.installedApps()
        open()
        urlField.forceActiveFocus()
    }

    readonly property string target: kind === "url" ? urlField.text
                                   : kind === "file" ? fileField.text
                                   : kind === "app" ? chosenApp
                                   : commandField.text

    title: i18n("Open when I start this")
    preferredWidth: Kirigami.Units.gridUnit * 28
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: i18n("Add")
            icon.name: "list-add"
            enabled: launcherDialog.target.trim() !== ""
            onTriggered: {
                if (AppController.addAction(launcherDialog.taskId, launcherDialog.kind, launcherDialog.target, onStart.checked) !== "")
                    launcherDialog.close()
            }
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Controls.ButtonGroup { id: kinds }
            Repeater {
                model: [
                    { key: "url", label: i18n("Website"), icon: "internet-web-browser" },
                    { key: "file", label: i18n("File"), icon: "document-open" },
                    { key: "app", label: i18n("App"), icon: "applications-other" },
                    { key: "command", label: i18n("Command"), icon: "utilities-terminal" }
                ]
                Controls.ToolButton {
                    required property var modelData
                    text: modelData.label
                    icon.name: modelData.icon
                    checkable: true
                    checked: launcherDialog.kind === modelData.key
                    Controls.ButtonGroup.group: kinds
                    onClicked: launcherDialog.kind = modelData.key
                }
            }
        }

        Controls.TextField {
            id: urlField
            visible: launcherDialog.kind === "url"
            Layout.fillWidth: true
            placeholderText: i18n("https://docs.example.com/the-report")
        }

        RowLayout {
            visible: launcherDialog.kind === "file"
            Layout.fillWidth: true
            Controls.TextField {
                id: fileField
                Layout.fillWidth: true
                placeholderText: i18n("A document, a folder, a playlist…")
            }
            Controls.Button {
                text: i18n("Choose…")
                icon.name: "document-open"
                onClicked: fileDialog.open()
            }
        }

        ColumnLayout {
            visible: launcherDialog.kind === "app"
            Layout.fillWidth: true
            Kirigami.SearchField {
                Layout.fillWidth: true
                placeholderText: i18n("Find an app")
                onTextChanged: launcherDialog.appFilter = text
            }
            ListView {
                id: appList
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 12
                clip: true
                model: launcherDialog.apps.filter(a => a.name.toLowerCase().includes(launcherDialog.appFilter.toLowerCase()))
                delegate: Controls.ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.name
                    icon.name: modelData.icon
                    highlighted: launcherDialog.chosenApp === modelData.id
                    onClicked: launcherDialog.chosenApp = modelData.id
                }
            }
        }

        ColumnLayout {
            visible: launcherDialog.kind === "command"
            Layout.fillWidth: true
            Controls.TextField {
                id: commandField
                Layout.fillWidth: true
                font.family: "monospace"
                placeholderText: i18n("kate ~/notes/report.md")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                text: i18n("Runs exactly this, without a shell (no pipes or variables; quote paths with spaces). You'll be asked once before it first runs, and again if it changes.")
            }
        }

        Controls.CheckBox {
            id: onStart
            text: i18n("Open automatically when I start this task")
            checked: true
        }
    }

    FileDialog {
        id: fileDialog
        onAccepted: fileField.text = selectedFile.toString()
    }
}
