import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// The weekly review: what the week held, then the few things worth a
// decision (what keeps moving, what's gone quiet, what's coming up), each
// with a one-tap answer. An AI reading of the week is there if wanted.
FormCard.FormCardPage {
    id: page

    signal openTask(string taskId)
    signal openNote(string noteId)

    property var week: AppController.weekReview()
    property var summary: null
    property string summaryError: ""
    property bool asking: false
    property bool allWins: false

    readonly property var wins: week.wins || []
    readonly property bool quiet: week.workedMinutes === 0 && wins.length === 0

    title: i18n("Weekly review")

    Connections {
        target: AppController
        function onChanged() { page.week = AppController.weekReview() }
        function onWeekSummaryReady(result) {
            page.asking = false
            page.summaryError = ""
            page.summary = result
        }
        function onAiFailed(feature, error) {
            if (feature !== "week")
                return
            page.asking = false
            page.summaryError = error
        }
    }

    function ask() {
        asking = true
        summaryError = ""
        AppController.summarizeWeek()
    }

    // ---- the week at a glance ---------------------------------------------------------
    FormCard.FormHeader {
        title: page.week.rangeLabel || ""
    }
    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width > Kirigami.Units.gridUnit * 30 ? 3 : 1
                    columnSpacing: Kirigami.Units.largeSpacing * 2
                    rowSpacing: Kirigami.Units.largeSpacing
                    Repeater {
                        model: [
                            { label: i18n("Time worked"), value: Format.minutes(page.week.workedMinutes || 0),
                              detail: i18np("in %1 session", "in %1 sessions", page.week.sessions || 0) },
                            { label: i18n("Tasks finished"), value: String(page.week.finishedTasks || 0), detail: "" },
                            { label: i18n("Routines done"), value: String(page.week.finishedRoutines || 0), detail: "" }
                        ]
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            Controls.Label {
                                text: modelData.label
                                color: Kirigami.Theme.disabledTextColor
                            }
                            Kirigami.Heading {
                                level: 2
                                text: modelData.value
                            }
                            Controls.Label {
                                visible: text !== ""
                                text: modelData.detail
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                            }
                        }
                    }
                }
                BarChart {
                    Layout.fillWidth: true
                    visible: !page.quiet
                    chartHeight: Kirigami.Units.gridUnit * 5
                    values: (page.week.days || []).map(d => d.workedMinutes)
                    labels: (page.week.days || []).map(d => d.label)
                    topLabels: (page.week.days || []).map(d => d.finished > 0 ? String(d.finished) : "")
                    tooltips: (page.week.days || []).map(d => i18n("%1: %2 worked, %3 finished", d.label, Format.minutes(d.workedMinutes), d.finished))
                }
                Controls.Label {
                    Layout.fillWidth: true
                    visible: page.quiet
                    wrapMode: Text.Wrap
                    text: i18n("A quiet week, at least as far as the timer knows. That's allowed.")
                }
            }
        }
    }

    // ---- an AI reading ------------------------------------------------------------------
    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing
        visible: AppController.ai.provider !== "off"
        FormCard.FormButtonDelegate {
            visible: page.summary === null
            icon.name: "tools-wizard"
            text: page.asking ? i18n("Thinking it over…")
                : AppController.ai.ready ? i18n("Ask %1 to reflect on the week", AppController.ai.label) : i18n("AI isn't set up yet")
            description: page.summaryError !== "" ? i18n("That didn't work: %1", page.summaryError)
                       : i18n("A short, kind look back and up to three things to try. Only task titles are shared unless you allow notes.")
            enabled: AppController.ai.ready && !page.asking
            trailingLogo.visible: false
            onClicked: page.ask()
        }
        FormCard.FormTextDelegate {
            visible: page.summary !== null
            icon.name: "tools-wizard"
            text: page.summary ? page.summary.summary : ""
            textItem.wrapMode: Text.Wrap
            textItem.elide: Text.ElideNone
            description: page.summary ? i18n("From %1", page.summary.by) : ""
        }
        Repeater {
            model: page.summary ? page.summary.suggestions : []
            delegate: ColumnLayout {
                id: suggestionRow
                required property string modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.FormTextDelegate {
                    icon.name: "help-hint"
                    text: suggestionRow.modelData
                    textItem.wrapMode: Text.Wrap
                    textItem.elide: Text.ElideNone
                }
            }
        }
        FormCard.FormDelegateSeparator { visible: page.summary !== null }
        FormCard.FormButtonDelegate {
            visible: page.summary !== null
            icon.name: "view-refresh"
            text: i18n("Ask again")
            enabled: !page.asking
            onClicked: { page.summary = null; page.ask() }
        }
    }

    // ---- done this week -------------------------------------------------------------------
    FormCard.FormHeader {
        visible: page.wins.length > 0
        title: i18n("Done this week")
    }
    FormCard.FormCard {
        visible: page.wins.length > 0
        Repeater {
            model: page.wins.slice(0, page.allWins ? undefined : 8)
            delegate: ColumnLayout {
                id: winRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator { visible: winRow.index > 0 }
                FormCard.FormButtonDelegate {
                    icon.name: "checkmark"
                    text: winRow.modelData.title
                    description: Qt.formatDate(new Date(winRow.modelData.at), "dddd")
                                 + (winRow.modelData.routine ? " · " + i18n("routine") : "")
                    trailingLogo.visible: false
                    onClicked: page.openTask(winRow.modelData.taskId)
                }
            }
        }
        FormCard.FormDelegateSeparator { visible: page.wins.length > 8 }
        FormCard.FormButtonDelegate {
            visible: page.wins.length > 8
            text: page.allWins ? i18n("Show fewer") : i18n("Show all %1", page.wins.length)
            onClicked: page.allWins = !page.allWins
        }
    }

    // ---- decisions --------------------------------------------------------------------------
    // One card per question, each row with a one-tap answer.
    component DecisionRow: ColumnLayout {
        id: row
        required property var modelData
        property string detail: ""
        property bool canLetGo: true
        Layout.fillWidth: true
        spacing: 0
        FormCard.FormDelegateSeparator {}
        FormCard.AbstractFormDelegate {
            Layout.fillWidth: true
            onClicked: page.openTask(row.modelData.taskId)
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Controls.Label {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        elide: Text.ElideRight
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        text: row.detail
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                }
                Controls.ToolButton {
                    icon.name: "view-calendar-day"
                    text: i18n("Next free slot")
                    display: page.width > Kirigami.Units.gridUnit * 30 ? Controls.AbstractButton.TextBesideIcon : Controls.AbstractButton.IconOnly
                    Controls.ToolTip.text: text
                    Controls.ToolTip.visible: hovered && display === Controls.AbstractButton.IconOnly
                    onClicked: AppController.scheduleNextSlot(row.modelData.taskId)
                }
                Controls.ToolButton {
                    visible: row.canLetGo
                    icon.name: "edit-delete-remove"
                    text: i18n("Let it go")
                    display: page.width > Kirigami.Units.gridUnit * 30 ? Controls.AbstractButton.TextBesideIcon : Controls.AbstractButton.IconOnly
                    Controls.ToolTip.text: Format.keys(i18n("Cancel the task (Ctrl+Z to undo)"))
                    Controls.ToolTip.visible: hovered
                    onClicked: AppController.letGo(row.modelData.taskId)
                }
            }
        }
    }

    FormCard.FormHeader {
        visible: (page.week.moving || []).length > 0
        title: i18n("Keeps getting moved")
    }
    FormCard.FormCard {
        visible: (page.week.moving || []).length > 0
        FormCard.FormTextDelegate {
            description: i18n("Moved more than once this week. Often the first step is too big: open it and break it down, give it a slot, or let it go.")
        }
        Repeater {
            model: page.week.moving || []
            delegate: DecisionRow {
                detail: i18np("Moved %1 time", "Moved %1 times", modelData.bumps)
            }
        }
    }

    FormCard.FormHeader {
        visible: (page.week.dueSoon || []).length > 0
        title: i18n("Due soon, not scheduled")
    }
    FormCard.FormCard {
        visible: (page.week.dueSoon || []).length > 0
        Repeater {
            model: page.week.dueSoon || []
            delegate: DecisionRow {
                detail: Format.dueLabel(modelData.due)
                canLetGo: false
            }
        }
    }

    FormCard.FormHeader {
        visible: (page.week.stale || []).length > 0
        title: i18n("Gone quiet")
    }
    FormCard.FormCard {
        visible: (page.week.stale || []).length > 0
        FormCard.FormTextDelegate {
            description: i18n("Untouched for a few weeks. Still wanted? Letting go of a few keeps the list honest.")
        }
        Repeater {
            model: page.week.stale || []
            delegate: DecisionRow {
                detail: i18np("Untouched for %1 day", "Untouched for %1 days", modelData.days)
            }
        }
    }

    // ---- your words -----------------------------------------------------------------------------
    FormCard.FormHeader {
        visible: (page.week.dayNotes || []).length > 0
        title: i18n("Your day notes")
    }
    FormCard.FormCard {
        visible: (page.week.dayNotes || []).length > 0
        Repeater {
            model: page.week.dayNotes || []
            delegate: ColumnLayout {
                id: noteRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator { visible: noteRow.index > 0 }
                FormCard.FormButtonDelegate {
                    text: noteRow.modelData.title
                    description: noteRow.modelData.content
                    onClicked: page.openNote(noteRow.modelData.id)
                }
            }
        }
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing * 2
        FormCard.FormButtonDelegate {
            icon.name: "dialog-ok-apply"
            text: page.week.doneToday ? i18n("Week reviewed") : i18n("Done reviewing")
            description: i18n("Next up: plan tomorrow, or just enjoy the rest of the day")
            onClicked: {
                AppController.finishReview()
                applicationWindow().showPage("today")
            }
        }
    }

    Item { Layout.preferredHeight: Kirigami.Units.gridUnit }
}
