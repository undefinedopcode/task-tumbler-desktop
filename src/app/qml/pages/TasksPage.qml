import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

Kirigami.ScrollablePage {
    id: page

    signal openTask(string taskId)

    title: i18n("Tasks")

    actions: [
        Kirigami.Action {
            text: i18n("Add")
            icon.name: "list-add"
            tooltip: Format.keys(i18n("Quick capture (Ctrl+N)"))
            onTriggered: applicationWindow().openCapture()
        }
    ]

    header: Controls.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Kirigami.SearchField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                placeholderText: i18n("Search titles, notes, #tags…")
                text: AppController.tasks.search
                onTextChanged: AppController.tasks.search = text
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [
                    { key: "active", label: i18n("To do") },
                    { key: "completed", label: i18n("Done") },
                    { key: "all", label: i18n("All") }
                ]
                Controls.ToolButton {
                    flat: true
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: AppController.tasks.filter === modelData.key
                    onClicked: AppController.tasks.filter = modelData.key
                    Controls.ButtonGroup.group: filterGroup
                }
            }
            Controls.ButtonGroup { id: filterGroup }
            Controls.ComboBox {
                id: categoryFilter
                model: [{ id: "", name: i18n("All categories") }].concat(AppController.categories)
                textRole: "name"
                valueRole: "id"
                onActivated: AppController.tasks.categoryId = currentValue
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(AppController.tasks.categoryId))
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: search.forceActiveFocus()
    }

    ListView {
        id: list
        model: AppController.tasks
        spacing: 0
        reuseItems: true
        delegate: TaskRow {
            onOpenTask: taskId => page.openTask(taskId)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: AppController.tasks.search ? "search" : "checkmark"
            text: AppController.tasks.search ? i18n("Nothing matches “%1”", AppController.tasks.search)
                : AppController.tasks.filter === "completed" ? i18n("Nothing finished yet")
                : i18n("Nothing to do")
            explanation: AppController.tasks.filter === "active" && !AppController.tasks.search
                         ? Format.keys(i18n("Enjoy it, or capture the next thing with Ctrl+N.")) : ""
        }
    }
}
