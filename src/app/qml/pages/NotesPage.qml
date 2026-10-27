import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// Every note, newest first; type to search titles and text.
Kirigami.ScrollablePage {
    id: page

    property string tag: ""
    property var notes: []
    property var tags: []

    signal openNote(string noteId)

    function reload() {
        notes = AppController.notes(search.text, tag)
        tags = AppController.noteTags()
        if (tag && tags.indexOf(tag) < 0)
            tag = ""
    }

    function newNote() {
        const id = AppController.createNote({ tags: page.tag ? [page.tag] : [] })
        if (id)
            page.openNote(id)
    }

    function when(ms) {
        const d = new Date(ms)
        const today = new Date()
        if (d.toDateString() === today.toDateString())
            return Format.time(ms)
        return Qt.formatDate(d, d.getFullYear() === today.getFullYear() ? "d MMM" : "d MMM yyyy")
    }

    title: i18n("Notes")
    Component.onCompleted: reload()
    onTagChanged: reload()

    Connections {
        target: AppController
        function onChanged(tables) { if (tables.indexOf("notes") >= 0 || tables.indexOf("tasks") >= 0) page.reload() }
    }

    actions: [
        Kirigami.Action {
            text: i18n("New note")
            icon.name: "list-add"
            shortcut: "Ctrl+Shift+N"
            onTriggered: page.newNote()
        }
    ]

    header: Controls.ToolBar {
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.SearchField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                placeholderText: i18n("Search notes…")
                delaySearch: true
                onAccepted: page.reload()
            }
            Flow {
                Layout.fillWidth: true
                visible: page.tags.length > 0
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: page.tags
                    Controls.ToolButton {
                        flat: true
                        required property string modelData
                        text: "#" + modelData
                        checkable: true
                        checked: page.tag === modelData
                        onClicked: page.tag = checked ? modelData : ""
                    }
                }
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: search.forceActiveFocus()
    }

    ListView {
        id: list
        model: page.notes
        spacing: Kirigami.Units.smallSpacing
        delegate: Controls.ItemDelegate {
            id: row
            required property var modelData
            width: ListView.view.width
            onClicked: page.openNote(modelData.id)
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 4
                    radius: 2
                    color: row.modelData.color || Kirigami.Theme.disabledTextColor
                    opacity: row.modelData.color ? 1 : 0.3
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Controls.Label {
                            text: page.when(row.modelData.updatedAt)
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: row.modelData.snippet
                        textFormat: Text.StyledText // matches come back in <b>
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        color: Kirigami.Theme.disabledTextColor
                    }
                    Flow {
                        Layout.fillWidth: true
                        visible: row.modelData.tags.length > 0 || row.modelData.taskTitle !== ""
                        spacing: Kirigami.Units.smallSpacing
                        Repeater {
                            model: row.modelData.tags
                            Chip {
                                required property string modelData
                                text: "#" + modelData
                            }
                        }
                        Chip {
                            visible: row.modelData.taskTitle !== ""
                            iconName: "view-task"
                            text: row.modelData.taskTitle
                        }
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: search.text ? "search" : "view-pim-notes"
            text: search.text ? i18n("Nothing matches “%1”", search.text) : (page.tag ? i18n("No notes tagged #%1", page.tag) : i18n("No notes yet"))
            explanation: search.text || page.tag ? "" : i18n("Somewhere for meeting notes, ideas and things to remember. Markdown works.")
            helpfulAction: search.text || page.tag ? null : newAction
            Kirigami.Action {
                id: newAction
                text: i18n("New note")
                icon.name: "list-add"
                onTriggered: page.newNote()
            }
        }
    }
}
