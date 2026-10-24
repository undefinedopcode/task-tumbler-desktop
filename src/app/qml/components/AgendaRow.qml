import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// One agenda entry on Today: a block, a recurring item or an event, with the
// actions that make sense for its kind and state right there on the row.
Controls.ItemDelegate {
    id: row

    required property var item
    signal openTask(string taskId)

    readonly property bool isSchedule: item.kind === "schedule"
    readonly property bool isOccurrence: item.kind === "occurrence"
    readonly property bool isEvent: item.kind === "event"
    readonly property bool finished: item.state === "done" || item.state === "skipped"
    readonly property color accent: isEvent ? (item.color || Kirigami.Theme.disabledTextColor)
                                            : (item.state === "overdue" ? Format.priorityColors.urgent
                                                                        : Format.priorityColors[item.priority])

    Layout.fillWidth: true
    hoverEnabled: true
    onClicked: item.taskId ? row.openTask(item.taskId) : applicationWindow().editEvent(item.id)

    background: Rectangle {
        radius: Kirigami.Units.cornerRadius
        color: row.item.working ? Qt.alpha(Kirigami.Theme.positiveTextColor, 0.10)
             : row.item.state === "now" ? Qt.alpha(Kirigami.Theme.highlightColor, 0.10)
             : row.hovered ? Qt.alpha(Kirigami.Theme.textColor, 0.05) : "transparent"
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing
        opacity: row.finished ? 0.55 : 1

        Rectangle {
            Layout.preferredWidth: 4
            Layout.fillHeight: true
            radius: 2
            color: row.accent
        }

        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4.5
            Controls.Label {
                text: row.item.timeless ? i18n("Any time") : Format.time(row.item.start)
                font.bold: row.item.state === "now"
                font.features: { "tnum": 1 }
            }
            Controls.Label {
                visible: !row.item.timeless
                text: Format.time(row.item.end)
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2
            Controls.Label {
                Layout.fillWidth: true
                text: row.item.title
                elide: Text.ElideRight
                font.strikeout: row.item.state === "done"
                font.weight: row.item.state === "now" ? Font.DemiBold : Font.Normal
            }
            RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Chip {
                    visible: row.item.working
                    text: i18n("Working on it")
                    iconName: "chronometer-start"
                    tint: Kirigami.Theme.positiveTextColor
                    strong: true
                }
                Chip {
                    visible: row.item.startsSoon && !row.finished
                    text: i18n("Starts soon")
                    tint: Kirigami.Theme.neutralTextColor
                    strong: true
                }
                Chip {
                    visible: row.item.state === "overdue"
                    text: i18n("Missed")
                    tint: Format.priorityColors.urgent
                    strong: true
                }
                Chip {
                    visible: row.isOccurrence
                    text: i18n("Repeats")
                    iconName: "view-refresh"
                }
                Chip {
                    visible: row.isEvent
                    text: i18n("Event")
                    iconName: "view-calendar"
                }
                Chip {
                    visible: row.item.state === "skipped"
                    text: i18n("Skipped")
                }
                Chip {
                    visible: row.item.bumpedCount > 0
                    text: i18np("Moved once", "Moved %1 times", row.item.bumpedCount)
                }
            }
        }

        // Actions: one obvious primary button, the rest one click away.
        Controls.ToolButton {
            visible: row.isSchedule && !row.finished && !row.item.working
            icon.name: "media-playback-start"
            text: i18n("Start")
            display: Controls.AbstractButton.IconOnly
            onClicked: AppController.startWork(row.item.taskId)
            Controls.ToolTip.text: i18n("Start working on this")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        Controls.ToolButton {
            visible: (row.isSchedule || row.isOccurrence) && !row.finished
            icon.name: "checkmark"
            text: i18n("Done")
            display: Controls.AbstractButton.IconOnly
            onClicked: row.isSchedule ? AppController.completeSchedule(row.item.id)
                                      : AppController.setOccurrence(row.item.id, "completed")
            Controls.ToolTip.text: i18n("Mark done")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        Controls.ToolButton {
            visible: row.isSchedule && row.item.state === "overdue"
            icon.name: "go-next"
            text: i18n("Later")
            onClicked: AppController.bumpToNextSlot(row.item.id)
            Controls.ToolTip.text: i18n("Move to the next free slot")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        Controls.ToolButton {
            visible: row.isOccurrence && row.finished
            icon.name: "edit-undo"
            text: i18n("Undo")
            display: Controls.AbstractButton.IconOnly
            onClicked: AppController.setOccurrence(row.item.id, "pending")
        }
        Controls.ToolButton {
            visible: !row.isEvent && !row.finished
            icon.name: "overflow-menu"
            display: Controls.AbstractButton.IconOnly
            text: i18n("More")
            onClicked: menu.popup()

            Controls.Menu {
                id: menu
                Controls.MenuItem {
                    visible: row.isSchedule
                    height: visible ? implicitHeight : 0
                    text: i18n("15 minutes later")
                    icon.name: "chronometer"
                    onTriggered: AppController.bump(row.item.id, 15)
                }
                Controls.MenuItem {
                    visible: row.isSchedule
                    height: visible ? implicitHeight : 0
                    text: i18n("An hour later")
                    onTriggered: AppController.bump(row.item.id, 60)
                }
                Controls.MenuItem {
                    visible: row.isSchedule
                    height: visible ? implicitHeight : 0
                    text: i18n("Next free slot")
                    icon.name: "go-next"
                    onTriggered: AppController.bumpToNextSlot(row.item.id)
                }
                Controls.MenuItem {
                    visible: row.isSchedule
                    height: visible ? implicitHeight : 0
                    text: i18n("Tomorrow")
                    icon.name: "go-next-skip"
                    onTriggered: AppController.bumpToTomorrow(row.item.id)
                }
                Controls.MenuItem {
                    visible: row.isOccurrence
                    height: visible ? implicitHeight : 0
                    text: i18n("Skip today")
                    icon.name: "go-next-skip"
                    onTriggered: AppController.setOccurrence(row.item.id, "skipped")
                }
                Controls.MenuSeparator {}
                Controls.MenuItem {
                    visible: row.isSchedule
                    height: visible ? implicitHeight : 0
                    text: i18n("Remove from schedule")
                    icon.name: "edit-delete-remove"
                    onTriggered: AppController.unschedule(row.item.id)
                }
                Controls.MenuItem {
                    text: i18n("Open task")
                    icon.name: "document-open"
                    onTriggered: row.openTask(row.item.taskId)
                }
            }
        }
    }
}
