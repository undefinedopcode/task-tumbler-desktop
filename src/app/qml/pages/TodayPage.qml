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
    // When a page is pushed on top, this one slides off to the left, partly
    // under the drawer, and its cards would draw over it. Hide it while it's
    // out of view.
    visible: !Kirigami.ColumnView.view || Kirigami.ColumnView.inViewport

    actions: [
        Kirigami.Action {
            text: i18n("Plan my day")
            icon.name: "view-calendar-week"
            tooltip: i18n("Fill free time with what matters most (Ctrl+P)")
            onTriggered: applicationWindow().openPlan()
        },
        Kirigami.Action {
            text: i18n("Add")
            icon.name: "list-add"
            tooltip: i18n("Quick capture (Ctrl+N)")
            onTriggered: Qt.callLater(() => applicationWindow().openCapture())
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: AppController.away.minutes !== undefined
            type: Kirigami.MessageType.Information
            icon.name: "chronometer-pause"
            text: i18n("Welcome back. You were away for %1 min. Still working on “%2”?",
                       AppController.away.minutes || 0, AppController.away.title || "")
            actions: [
                Kirigami.Action {
                    text: i18n("Yes, keep the time")
                    onTriggered: AppController.resolveAway("keep")
                },
                Kirigami.Action {
                    text: i18n("Leave the break out")
                    onTriggered: AppController.resolveAway("trimRestart")
                },
                Kirigami.Action {
                    text: i18n("Stop at %1", AppController.away.sinceLabel || "")
                    onTriggered: AppController.resolveAway("trim")
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.today.closeOutDue === true
            type: Kirigami.MessageType.Positive
            icon.name: "weather-clear-night"
            text: i18n("Winding down? Take a couple of minutes to close out the day.")
            actions: [
                Kirigami.Action {
                    text: i18n("Close out")
                    icon.name: "go-next"
                    onTriggered: applicationWindow().showPage("closeout")
                },
                Kirigami.Action {
                    text: i18n("Not today")
                    onTriggered: AppController.finishCloseOut(true)
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.today.reviewDue === true && page.today.closeOutDue !== true
            type: Kirigami.MessageType.Information
            icon.name: "view-calendar-month"
            text: i18n("Time for the weekly review: a few minutes to look back and tidy up.")
            actions: [
                Kirigami.Action {
                    text: i18n("Review the week")
                    icon.name: "go-next"
                    onTriggered: applicationWindow().showPage("review")
                },
                Kirigami.Action {
                    text: i18n("Skip this week")
                    onTriggered: AppController.finishReview(true)
                }
            ]
        }

        WorkingCard {
            onOpenTask: taskId => page.openTask(taskId)
        }

        EnergyCheckIn {
            visible: AppController.energy.due === true && AppController.working.id === undefined
        }

        // Energy and all-day events as a quiet strip.
        Flow {
            Layout.fillWidth: true
            visible: page.today.allDay.length > 0 || AppController.energy.level !== undefined
            spacing: Kirigami.Units.smallSpacing
            Chip {
                visible: AppController.energy.level !== undefined
                text: i18n("Energy: %1", AppController.energy.label || "")
                iconName: "battery-060"
                tint: (AppController.energy.level || 0) <= 4 ? Format.priorityColors.high : Kirigami.Theme.positiveTextColor
                strong: true
                TapHandler { onTapped: applicationWindow().showPage("energy") }
            }
            Repeater {
                model: page.today.allDay
                Chip {
                    required property var modelData
                    text: modelData.title
                    iconName: "view-calendar"
                    tint: modelData.color || Kirigami.Theme.highlightColor
                    strong: true
                    TapHandler { onTapped: applicationWindow().editEvent(parent.modelData.id) }
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
