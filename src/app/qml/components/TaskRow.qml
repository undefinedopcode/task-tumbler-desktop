import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// A task in the Tasks list. Fed by TaskListModel roles.
Controls.ItemDelegate {
    id: row

    required property string taskId
    required property string title
    required property string status
    required property string priority
    required property string energy
    required property int estimate
    required property string dueDate
    required property string dueState
    required property var tags
    required property string categoryName
    required property string categoryColor
    required property bool isRecurring
    required property string recurrenceText
    required property bool blocked
    required property int subtasksDone
    required property int subtasksTotal
    required property string parentTitle
    required property double nextBlock

    readonly property bool finished: status === "completed" || status === "cancelled"
    signal openTask(string taskId)

    width: ListView.view ? ListView.view.width : implicitWidth
    onClicked: row.openTask(taskId)

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing

        Controls.CheckBox {
            checked: row.status === "completed"
            onToggled: AppController.setTaskStatus(row.taskId, checked ? "completed" : "pending")
            Controls.ToolTip.text: checked ? i18n("Reopen") : i18n("Mark done")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        Rectangle {
            Layout.preferredWidth: 4
            Layout.fillHeight: true
            radius: 2
            color: Format.priorityColors[row.priority]
            opacity: row.finished ? 0.3 : 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2

            Controls.Label {
                Layout.fillWidth: true
                text: row.parentTitle ? row.parentTitle + "  ›  " + row.title : row.title
                elide: Text.ElideRight
                font.strikeout: row.status === "completed"
                color: row.finished || row.blocked ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
            }

            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Chip {
                    visible: row.status === "in_progress"
                    text: i18n("In progress")
                    tint: Kirigami.Theme.positiveTextColor
                    strong: true
                }
                Chip {
                    visible: row.blocked
                    text: i18n("Waiting on something")
                    iconName: "chronometer-pause"
                    tint: Kirigami.Theme.neutralTextColor
                    strong: true
                }
                Chip {
                    visible: row.dueDate !== ""
                    text: Format.dueLabel(row.dueDate)
                    iconName: "view-calendar"
                    tint: row.dueState === "overdue" ? Format.priorityColors.urgent
                        : row.dueState === "today" ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.disabledTextColor
                    strong: row.dueState === "overdue" || row.dueState === "today"
                }
                Chip {
                    visible: row.nextBlock > 0
                    text: Format.dueLabel(Format.isoDate(new Date(row.nextBlock))) + " " + Format.time(row.nextBlock)
                    iconName: "view-calendar-day"
                    tint: Kirigami.Theme.highlightColor
                    strong: true
                }
                Chip { text: Format.minutes(row.estimate); iconName: "chronometer" }
                Chip { visible: row.energy === "high" || row.energy === "peak"; text: Format.energyLabel(row.energy) }
                Chip { visible: row.isRecurring; text: row.recurrenceText; iconName: "view-refresh" }
                Chip {
                    visible: row.subtasksTotal > 0
                    text: i18n("%1/%2 steps", row.subtasksDone, row.subtasksTotal)
                }
                Chip {
                    visible: row.categoryName !== ""
                    text: row.categoryName
                    tint: row.categoryColor || Kirigami.Theme.disabledTextColor
                }
                Repeater {
                    model: row.tags
                    Chip {
                        required property string modelData
                        text: "#" + modelData
                    }
                }
            }
        }

        Controls.ToolButton {
            flat: true
            visible: !row.finished && !row.blocked
            icon.name: "media-playback-start"
            display: Controls.AbstractButton.IconOnly
            text: i18n("Start")
            onClicked: AppController.startWork(row.taskId)
            Controls.ToolTip.text: i18n("Start working on this")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        Controls.ToolButton {
            flat: true
            visible: !row.finished && !row.isRecurring && row.nextBlock === 0
            icon.name: "view-calendar-day"
            display: Controls.AbstractButton.IconOnly
            text: i18n("Schedule")
            onClicked: AppController.scheduleNextSlot(row.taskId)
            Controls.ToolTip.text: i18n("Put it in the next free slot")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }
}
