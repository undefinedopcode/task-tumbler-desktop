import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// One note. Saves itself a moment after you stop typing, and when you leave.
Kirigami.Page {
    id: page

    required property string noteId
    property var note: AppController.noteDetail(noteId)
    property bool dirty: false
    property bool preview: false
    readonly property var swatches: ["", "#6366f1", "#0ea5e9", "#10b981", "#f59e0b", "#ef4444", "#ec4899", "#a855f7"]

    signal openTask(string taskId)

    function save() {
        if (!dirty || !note.id)
            return
        dirty = false
        AppController.saveNote(noteId, {
            title: titleField.text,
            content: body.text,
            tags: tagsField.text.split(",").map(t => t.trim().replace(/^#+/, "")).filter(t => t.length > 0)
        })
    }
    function edited() {
        dirty = true
        saveTimer.restart()
    }

    title: note.title || i18n("Note")
    padding: 0

    Component.onCompleted: {
        if (note.title === i18n("Untitled note") && !note.content)
            titleField.selectAll()
        (note.content ? body : titleField).forceActiveFocus()
    }
    Component.onDestruction: save()

    Timer {
        id: saveTimer
        interval: 800
        onTriggered: page.save()
    }
    // The note was changed elsewhere (undo of a delete, say): pick that up
    // unless there are edits here not yet saved.
    Connections {
        target: AppController
        function onChanged(tables) {
            if (tables.indexOf("notes") < 0 || page.dirty)
                return
            const fresh = AppController.noteDetail(page.noteId)
            if (!fresh.id) {
                applicationWindow().pageStack.pop()
                return
            }
            if (fresh.updatedAt !== page.note.updatedAt && fresh.content !== body.text) {
                page.note = fresh
                body.text = fresh.content
                titleField.text = fresh.title
            } else {
                page.note = fresh
            }
        }
    }

    actions: [
        Kirigami.Action {
            text: page.preview ? i18n("Edit") : i18n("Preview")
            icon.name: page.preview ? "document-edit" : "view-preview"
            checkable: true
            checked: page.preview
            shortcut: "Ctrl+Shift+P"
            onTriggered: page.preview = !page.preview
        },
        Kirigami.Action {
            text: i18n("Export…")
            icon.name: "document-export"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: { page.save(); exportDialog.open() }
        },
        Kirigami.Action {
            text: i18n("Delete")
            icon.name: "edit-delete"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: {
                page.dirty = false
                saveTimer.stop()
                AppController.deleteNote(page.noteId)
            }
        }
    ]

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("Markdown (*.md)")]
        defaultSuffix: "md"
        selectedFile: "file:///" + (titleField.text || "note").replace(/[\/\\:*?"<>|]+/g, "-") + ".md"
        onAccepted: AppController.exportNote(page.noteId, selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Controls.TextField {
                id: titleField
                Layout.fillWidth: true
                text: page.note.title || ""
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4
                font.weight: Font.DemiBold
                placeholderText: i18n("Title")
                background: null
                onTextEdited: page.edited()
                Keys.onReturnPressed: body.forceActiveFocus()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: "tag"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: implicitWidth
                }
                Controls.TextField {
                    id: tagsField
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 14
                    text: (page.note.tags || []).join(", ")
                    placeholderText: i18n("Tags, comma separated")
                    onTextEdited: page.edited()
                }
                Kirigami.Icon {
                    source: "view-task"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: implicitWidth
                }
                Controls.ComboBox {
                    id: taskPicker
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                    // Open tasks, plus the linked one even if it's finished.
                    model: {
                        const open = AppController.noteTaskChoices()
                        if (page.note.taskId && !open.some(t => t.id === page.note.taskId))
                            open.unshift({ id: page.note.taskId, title: page.note.taskTitle })
                        return [{ id: "", title: i18n("Not about a task") }].concat(open)
                    }
                    textRole: "title"
                    valueRole: "id"
                    currentIndex: Math.max(0, model.findIndex(t => t.id === (page.note.taskId || "")))
                    onActivated: {
                        page.save()
                        AppController.saveNote(page.noteId, { taskId: currentValue })
                    }
                }
                Controls.ToolButton {
                    flat: true
                    visible: !!page.note.taskId
                    icon.name: "go-next"
                    text: i18n("Open the task")
                    display: Controls.AbstractButton.IconOnly
                    onClicked: page.openTask(page.note.taskId)
                    Controls.ToolTip.text: text
                    Controls.ToolTip.visible: hovered
                    Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                Repeater {
                    model: page.swatches
                    Rectangle {
                        required property string modelData
                        width: Kirigami.Units.gridUnit
                        height: width
                        radius: width / 2
                        color: modelData || "transparent"
                        border.width: (page.note.color || "") === modelData ? 2 : 1
                        border.color: (page.note.color || "") === modelData ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        // The first swatch is "no colour".
                        Rectangle {
                            visible: parent.modelData === ""
                            anchors.centerIn: parent
                            width: parent.width * 0.9
                            height: 1.5
                            rotation: -45
                            color: Kirigami.Theme.disabledTextColor
                        }
                        TapHandler {
                            onTapped: {
                                page.save()
                                AppController.saveNote(page.noteId, { color: parent.modelData })
                            }
                        }
                    }
                }
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !page.preview
            Controls.TextArea {
                id: body
                text: page.note.content || ""
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                placeholderText: i18n("Write here. Markdown works: # headings, **bold**, - lists, [links](https://…)")
                background: null
                leftPadding: Kirigami.Units.largeSpacing * 2
                rightPadding: Kirigami.Units.largeSpacing * 2
                topPadding: Kirigami.Units.largeSpacing
                onTextChanged: if (activeFocus) page.edited()
            }
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.preview
            Controls.Label {
                width: page.width
                padding: Kirigami.Units.largeSpacing * 2
                text: body.text || i18n("*Nothing written yet.*")
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                onLinkActivated: link => Qt.openUrlExternally(link)
                HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
        }
    }
}
