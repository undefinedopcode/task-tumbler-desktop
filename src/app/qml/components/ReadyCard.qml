import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// A task that could be picked up right now: nothing blocks it and it has no
// block on the schedule yet.
Kirigami.AbstractCard {
    id: card

    required property var task
    signal openTask(string taskId)

    showClickFeedback: true
    onClicked: card.openTask(task.id)

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.smallSpacing * 1.5
                Layout.preferredHeight: Kirigami.Units.smallSpacing * 1.5
                radius: width / 2
                color: Format.priorityColors[card.task.priority]
            }
            Controls.Label {
                Layout.fillWidth: true
                text: card.task.title
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.Wrap
                font.weight: Font.DemiBold
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Chip {
                visible: card.task.status === "in_progress"
                text: i18n("In progress")
                tint: Kirigami.Theme.positiveTextColor
                strong: true
            }
            Chip { text: Format.minutes(card.task.estimate); iconName: "chronometer" }
            Chip { text: Format.energyLabel(card.task.energy) }
            Chip {
                visible: card.task.dueDate !== ""
                text: Format.dueLabel(card.task.dueDate)
                iconName: "view-calendar"
            }
            Chip {
                visible: card.task.subtasksTotal > 0
                text: i18n("%1/%2 steps", card.task.subtasksDone, card.task.subtasksTotal)
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: i18n("Start")
                icon.name: "media-playback-start"
                onClicked: AppController.startWork(card.task.id)
            }
            Controls.Button {
                text: i18n("Schedule")
                icon.name: "view-calendar-day"
                onClicked: AppController.scheduleNextSlot(card.task.id)
                Controls.ToolTip.text: i18n("Put it in the next free slot")
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            Item { Layout.fillWidth: true }
            Controls.ToolButton {
                flat: true
                icon.name: "checkmark"
                display: Controls.AbstractButton.IconOnly
                text: i18n("Done")
                onClicked: AppController.setTaskStatus(card.task.id, "completed")
                Controls.ToolTip.text: i18n("Already done")
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }
    }
}
