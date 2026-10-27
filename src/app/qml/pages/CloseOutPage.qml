import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// Closing out the day: what got done (a list, not a score), one tap for
// each loose end, a line to remember, and a look at tomorrow.
FormCard.FormCardPage {
    id: page

    signal openTask(string taskId)

    property var day: AppController.closeOut()
    readonly property var leftovers: day.leftovers || []
    readonly property var wins: day.wins || []
    readonly property var progress: day.progress || []
    readonly property var tomorrow: day.tomorrow || ({})

    title: i18n("Close out the day")

    Connections {
        target: AppController
        function onChanged() { page.day = AppController.closeOut() }
    }

    function leftoverWhen(l) {
        if (l.kind === "routine")
            return l.timeless ? i18n("Routine, any time today") : i18n("Routine at %1", Format.time(l.start))
        const when = l.earlier ? i18nc("day and time, e.g. Mon 2 Mar 09:00", "%1 %2", l.dayLabel, Format.time(l.start))
                               : Format.time(l.start)
        return l.bumps > 1 ? i18np("%2 · moved %1 time", "%2 · moved %1 times", l.bumps, when) : when
    }

    function tomorrowText() {
        const t = page.tomorrow
        const parts = []
        if (t.blocks > 0)
            parts.push(i18np("%1 block", "%1 blocks", t.blocks))
        if (t.events > 0)
            parts.push(i18np("%1 event", "%1 events", t.events))
        if (t.routines > 0)
            parts.push(i18np("%1 routine", "%1 routines", t.routines))
        return parts.length ? parts.join(", ") : i18n("Nothing planned yet")
    }

    // ---- what got done --------------------------------------------------------
    FormCard.FormHeader {
        title: page.day.dateLabel || ""
    }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            icon.name: page.wins.length > 0 ? "emblem-success" : "weather-few-clouds"
            text: page.wins.length > 0 ? i18np("You finished %1 thing today", "You finished %1 things today", page.wins.length)
                : page.progress.length > 0 ? i18n("You moved things forward today")
                : i18n("A quiet day")
            description: page.day.workedMinutes > 0 ? i18n("%1 of focused time logged.", Format.minutes(page.day.workedMinutes))
                : page.wins.length > 0 || page.progress.length > 0 ? ""
                : i18n("Some days are like that. Rest counts too.")
        }
        Repeater {
            model: page.wins
            delegate: ColumnLayout {
                id: winRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.FormButtonDelegate {
                    icon.name: "checkmark"
                    text: winRow.modelData.title
                    description: winRow.modelData.routine ? i18n("Routine")
                               : winRow.modelData.workedMinutes > 0 ? i18n("%1 in all", Format.minutes(winRow.modelData.workedMinutes)) : ""
                    trailingLogo.visible: false
                    onClicked: page.openTask(winRow.modelData.taskId)
                }
            }
        }
        Repeater {
            model: page.progress
            delegate: ColumnLayout {
                id: progressRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.FormButtonDelegate {
                    icon.name: "chronometer"
                    text: progressRow.modelData.title
                    description: i18n("Made progress: %1 today", Format.minutes(progressRow.modelData.minutes))
                    trailingLogo.visible: false
                    onClicked: page.openTask(progressRow.modelData.taskId)
                }
            }
        }
    }

    // ---- still running ----------------------------------------------------------
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 30
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Kirigami.Units.largeSpacing
        visible: AppController.working.id !== undefined
        type: Kirigami.MessageType.Information
        icon.name: "chronometer"
        text: i18n("The timer is still running on “%1”.", AppController.working.title || "")
        actions: Kirigami.Action {
            text: i18n("Stop it")
            icon.name: "media-playback-stop"
            onTriggered: AppController.stopWork()
        }
    }

    // ---- loose ends -----------------------------------------------------------------
    FormCard.FormHeader {
        visible: page.leftovers.length > 0
        title: i18n("Still open")
    }
    FormCard.FormCard {
        visible: page.leftovers.length > 0
        FormCard.FormTextDelegate {
            description: i18n("None of these has to happen today. Pick where each one goes, or clear them all and let tomorrow's plan pick them up.")
        }
        Repeater {
            model: page.leftovers
            delegate: ColumnLayout {
                id: leftoverRow
                required property var modelData
                readonly property bool routine: modelData.kind === "routine"
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.AbstractFormDelegate {
                    Layout.fillWidth: true
                    onClicked: page.openTask(leftoverRow.modelData.taskId)
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Controls.Label {
                            Layout.fillWidth: true
                            text: leftoverRow.modelData.title
                            elide: Text.ElideRight
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: page.leftoverWhen(leftoverRow.modelData)
                            font: Kirigami.Theme.smallFont
                            color: leftoverRow.modelData.earlier ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.disabledTextColor
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Repeater {
                                model: leftoverRow.routine
                                    ? [{ choice: "done", text: i18n("Done"), icon: "dialog-ok-apply", tip: i18n("It did happen") },
                                       { choice: "skip", text: i18n("Skip today"), icon: "media-skip-forward", tip: i18n("It comes round again anyway") }]
                                    : [{ choice: "tomorrow", text: i18n("Tomorrow"), icon: "go-next", tip: i18n("The first free slot tomorrow") },
                                       { choice: "pile", text: i18n("Back to the pile"), icon: "view-task", tip: i18n("Unschedule it; it stays on your list") },
                                       { choice: "drop", text: i18n("Let it go"), icon: "edit-delete-remove", tip: Format.keys(i18n("Cancel the task (Ctrl+Z to undo)")) },
                                       { choice: "done", text: i18n("Did it"), icon: "dialog-ok-apply", tip: i18n("Mark it done") }]
                                Controls.Button {
                                    required property var modelData
                                    text: modelData.text
                                    icon.name: modelData.icon
                                    flat: true
                                    Controls.ToolTip.text: modelData.tip
                                    Controls.ToolTip.visible: hovered
                                    Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                                    onClicked: AppController.resolveLeftover(leftoverRow.modelData.kind, leftoverRow.modelData.id, modelData.choice)
                                }
                            }
                        }
                    }
                }
            }
        }
        FormCard.FormDelegateSeparator { visible: page.leftovers.length > 1 }
        FormCard.FormButtonDelegate {
            visible: page.leftovers.length > 1
            icon.name: "edit-clear-all"
            text: i18n("Clear the rest")
            description: i18n("Unschedule them all and skip today's routines, in one step")
            onClicked: AppController.clearLeftovers()
        }
    }

    // ---- a line to remember ---------------------------------------------------------
    FormCard.FormHeader {
        title: i18n("Anything to remember about today?")
    }
    FormCard.FormCard {
        FormCard.FormTextAreaDelegate {
            id: note
            label: i18n("Day note")
            placeholderText: i18n("What helped, what got in the way, what's on your mind… (saved to Notes)")
            // Once, not bound: saving refreshes `day`, and that mustn't move the cursor.
            Component.onCompleted: text = page.day.note || ""
            onTextChanged: if (activeFocus) saveTimer.restart()
            Timer {
                id: saveTimer
                interval: 800
                onTriggered: AppController.saveDayNote(note.text)
            }
            Component.onDestruction: if (saveTimer.running) AppController.saveDayNote(note.text)
        }
    }

    // ---- tomorrow ---------------------------------------------------------------------
    FormCard.FormHeader {
        title: i18n("Tomorrow")
    }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            icon.name: "view-calendar-day"
            text: page.tomorrow.label || ""
            description: page.tomorrowText()
                         + (page.tomorrow.firstTitle ? " · " + i18n("first up: %1 at %2", page.tomorrow.firstTitle, Format.time(page.tomorrow.firstAt)) : "")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            icon.name: "view-calendar-week"
            text: i18n("Plan tomorrow")
            description: i18n("Fill tomorrow's free time from the pile")
            onClicked: applicationWindow().openPlan(page.tomorrow.date)
        }
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing * 2
        FormCard.FormButtonDelegate {
            icon.name: "weather-clear-night"
            text: page.day.closed ? i18n("Day closed") : i18n("Close the day")
            description: page.day.closed ? i18n("You can come back and change things any time.")
                                         : i18n("That's it for today")
            onClicked: {
                if (saveTimer.running) {
                    saveTimer.stop()
                    AppController.saveDayNote(note.text)
                }
                AppController.finishCloseOut()
                applicationWindow().showPage("today")
            }
        }
    }

    Item { Layout.preferredHeight: Kirigami.Units.gridUnit }
}
