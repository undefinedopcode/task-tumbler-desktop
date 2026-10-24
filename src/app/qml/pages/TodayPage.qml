import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// Home: what's happening now, what got missed, what's next, and what could
// be picked up. Ordered so the most actionable thing is always at the top.
Kirigami.ScrollablePage {
    id: page

    readonly property var today: AppController.today
    readonly property bool nothingAtAll: today.now.length === 0 && today.upcoming.length === 0
                                         && today.overdue.length === 0 && today.done.length === 0
                                         && today.ready.length === 0 && AppController.working.id === undefined
    property bool showDone: false

    signal openTask(string taskId)

    title: today.dateLabel || i18n("Today")

    actions: [
        Kirigami.Action {
            text: i18n("Add")
            icon.name: "list-add"
            tooltip: i18n("Quick capture (Ctrl+N)")
            onTriggered: Qt.callLater(() => applicationWindow().openCapture())
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        WorkingCard {
            onOpenTask: taskId => page.openTask(taskId)
        }

        // All-day events as a quiet strip.
        Flow {
            Layout.fillWidth: true
            visible: page.today.allDay.length > 0
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                model: page.today.allDay
                Chip {
                    required property var modelData
                    text: modelData.title
                    iconName: "view-calendar"
                    tint: modelData.color || Kirigami.Theme.highlightColor
                    strong: true
                }
            }
        }

        SectionTitle {
            visible: page.today.now.length > 0
            text: i18n("Right now")
            accent: Kirigami.Theme.highlightColor
        }
        Repeater {
            model: page.today.now
            AgendaRow {
                required property var modelData
                item: modelData
                onOpenTask: taskId => page.openTask(taskId)
            }
        }

        SectionTitle {
            visible: page.today.overdue.length > 0
            text: i18n("Didn't happen yet")
            count: page.today.overdue.length
        }
        Controls.Label {
            visible: page.today.overdue.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("No problem. Move them to a free slot, do them now, or let them go.")
        }
        Repeater {
            model: page.today.overdue
            AgendaRow {
                required property var modelData
                item: modelData
                onOpenTask: taskId => page.openTask(taskId)
            }
        }

        SectionTitle {
            visible: page.today.upcoming.length > 0
            text: i18n("Coming up")
            count: page.today.upcoming.length
        }
        Repeater {
            model: page.today.upcoming
            AgendaRow {
                required property var modelData
                item: modelData
                onOpenTask: taskId => page.openTask(taskId)
            }
        }

        SectionTitle {
            visible: page.today.ready.length > 0
            text: i18n("Ready to work")
            count: page.today.readyTotal
        }
        Controls.Label {
            visible: page.today.ready.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("Nothing is blocking these. Start one, or drop it into the next free slot.")
        }
        GridLayout {
            Layout.fillWidth: true
            visible: page.today.ready.length > 0
            columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 16)))
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            Repeater {
                model: page.today.ready
                ReadyCard {
                    required property var modelData
                    task: modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                    onOpenTask: taskId => page.openTask(taskId)
                }
            }
        }

        Controls.Button {
            visible: page.today.done.length > 0
            flat: true
            text: page.showDone ? i18n("Hide done (%1)", page.today.done.length)
                                : i18n("Done today (%1)", page.today.done.length)
            icon.name: page.showDone ? "arrow-up" : "arrow-down"
            onClicked: page.showDone = !page.showDone
        }
        Repeater {
            model: page.showDone ? page.today.done : []
            AgendaRow {
                required property var modelData
                item: modelData
                onOpenTask: taskId => page.openTask(taskId)
            }
        }

        Kirigami.PlaceholderMessage {
            visible: page.nothingAtAll
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 4
            icon.source: "qrc:/icons/org.kde.tasktumbler.svg"
            text: i18n("A clear day")
            explanation: i18n("Nothing scheduled and nothing waiting. Capture whatever's on your mind with Ctrl+N, or from anywhere with Meta+Shift+Space.")
            helpfulAction: Kirigami.Action {
                text: i18n("Capture something")
                icon.name: "list-add"
                onTriggered: applicationWindow().openCapture()
            }
        }

        Item { Layout.preferredHeight: Kirigami.Units.gridUnit }
    }
}
