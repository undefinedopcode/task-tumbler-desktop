import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// Looking back: time worked, things finished, the hours that work for you,
// and how long things take compared with your guesses. Nothing here is a
// score; it's for planning better.
FormCard.FormCardPage {
    id: page

    signal openTask(string taskId)

    property int days: 7
    property var stats: AppController.stats(days)
    property bool allTasks: false

    readonly property bool empty: stats.workedMinutes === 0 && stats.finishedTasks === 0 && stats.finishedRoutines === 0
    readonly property var estimates: stats.estimates || ({})
    // The local hours worth showing: wherever there's work or a check-in,
    // and at least the working day.
    readonly property var hourRange: {
        let first = 8, last = 19
        for (const h of stats.hours || []) {
            if (h.minutes > 0 || (h.energy !== undefined && h.energy !== null)) {
                first = Math.min(first, h.hour)
                last = Math.max(last, h.hour)
            }
        }
        return (stats.hours || []).slice(first, last + 1)
    }
    // The two hours in a row with the most work.
    readonly property int bestHour: {
        const hours = stats.hours || []
        let best = -1, most = 0
        for (let h = 0; h < 24; ++h) {
            const sum = hours[h].minutes + hours[(h + 1) % 24].minutes
            if (sum > most) {
                most = sum
                best = h
            }
        }
        return most >= 30 ? best : -1
    }

    title: i18n("Stats")

    Connections {
        target: AppController
        function onChanged() { page.stats = AppController.stats(page.days) }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Week")
            checkable: true
            checked: page.days === 7
            onTriggered: page.days = 7
        },
        Kirigami.Action {
            text: i18n("Month")
            checkable: true
            checked: page.days === 30
            onTriggered: page.days = 30
        },
        Kirigami.Action {
            text: i18n("3 months")
            checkable: true
            checked: page.days === 90
            onTriggered: page.days = 90
        },
        Kirigami.Action {
            text: i18n("All time")
            checkable: true
            checked: page.days === 0
            onTriggered: page.days = 0
        }
    ]

    function ratioText(r) {
        return i18nc("how many times the estimate, e.g. 1.4×", "%1×", Number(r).toLocaleString(Qt.locale(), "f", 1))
    }

    function ratioColor(r) {
        return r > 1.25 ? Format.priorityColors.high : r < 0.8 ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.textColor
    }

    function hourText(h) {
        const d = new Date()
        d.setHours(h, 0, 0, 0)
        return Qt.formatTime(d, Qt.locale().timeFormat(Locale.ShortFormat))
    }

    // Compact, for the chart's axis: "9 AM" or "09".
    function hourAxisText(h) {
        const d = new Date()
        d.setHours(h, 0, 0, 0)
        return Qt.formatTime(d, /a/i.test(Qt.locale().timeFormat(Locale.ShortFormat)) ? "h AP" : "HH")
    }

    function sizeText(size) {
        return {
            "15": i18n("Quick tasks (up to 15 min)"),
            "30": i18n("Up to half an hour"),
            "60": i18n("Up to an hour"),
            "120": i18n("Up to two hours"),
            "longer": i18n("Longer than two hours")
        }[size] || size
    }

    FormCard.FormCard {
        visible: page.empty
        FormCard.FormTextDelegate {
            text: i18n("Nothing here yet")
            description: page.days === 0
                         ? i18n("Start the timer when you work on a task, and finish a few. This fills in as you go.")
                         : i18n("No work logged or tasks finished in this time. Try a longer range.")
        }
    }

    // ---- at a glance ------------------------------------------------------
    FormCard.FormCard {
        visible: !page.empty
        Layout.topMargin: Kirigami.Units.largeSpacing
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: GridLayout {
                columns: width > Kirigami.Units.gridUnit * 24 ? 3 : 1
                columnSpacing: Kirigami.Units.largeSpacing * 2
                rowSpacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: [
                        {
                            label: i18n("Time worked"),
                            value: Format.minutes(page.stats.workedMinutes),
                            detail: i18np("in %1 session", "in %1 sessions", page.stats.sessions)
                        },
                        {
                            label: i18n("Finished"),
                            value: i18np("%1 task", "%1 tasks", page.stats.finishedTasks),
                            detail: page.stats.finishedRoutines > 0 ? i18np("and %1 routine", "and %1 routines", page.stats.finishedRoutines) : ""
                        },
                        {
                            label: i18n("Typical session"),
                            value: page.stats.sessions > 0 ? Format.minutes(page.stats.averageSessionMinutes) : "–",
                            detail: page.stats.sessions > 0 ? i18n("longest %1", Format.minutes(page.stats.longestSessionMinutes)) : ""
                        }
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        Controls.Label {
                            text: parent.modelData.label
                            color: Kirigami.Theme.disabledTextColor
                        }
                        Kirigami.Heading {
                            level: 2
                            text: parent.modelData.value
                        }
                        Controls.Label {
                            text: parent.modelData.detail
                            visible: text !== ""
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }
            }
        }
    }

    // ---- day by day ---------------------------------------------------------
    FormCard.FormHeader {
        visible: !page.empty
        title: page.stats.weekly ? i18n("Week by week") : i18n("Day by day")
    }
    FormCard.FormCard {
        visible: !page.empty
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: ColumnLayout {
                BarChart {
                    Layout.fillWidth: true
                    values: (page.stats.bars || []).map(b => b.workedMinutes)
                    labels: (page.stats.bars || []).map(b => b.label)
                    topLabels: (page.stats.bars || []).map(b => b.finished > 0 && page.stats.bars.length <= 31 ? String(b.finished) : "")
                    tooltips: (page.stats.bars || []).map(b => i18n("%1: %2 worked, %3 finished", b.label, Format.minutes(b.workedMinutes), b.finished))
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18n("Bars are time worked; the numbers above them are things finished.")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }
    }

    // ---- hours of the day ---------------------------------------------------
    FormCard.FormHeader {
        visible: !page.empty
        title: i18n("Your hours")
    }
    FormCard.FormCard {
        visible: !page.empty
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: ColumnLayout {
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    visible: page.bestHour >= 0
                    text: i18n("You get the most done between %1 and %2.", page.hourText(page.bestHour), page.hourText((page.bestHour + 2) % 24))
                }
                BarChart {
                    Layout.fillWidth: true
                    values: page.hourRange.map(h => h.minutes)
                    labels: page.hourRange.map(h => page.hourAxisText(h.hour))
                    dots: page.hourRange.map(h => h.energy)
                    tooltips: page.hourRange.map(h => {
                        let text = i18n("%1: %2 worked", page.hourText(h.hour), Format.minutes(h.minutes))
                        if (h.energy !== undefined && h.energy !== null)
                            text += "\n" + i18n("Average energy %1 of 10", Number(h.energy).toLocaleString(Qt.locale(), "f", 1))
                        return text
                    })
                }
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18n("Bars are time worked in each hour; dots are your average energy check-in then.")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }
    }

    // ---- estimates ----------------------------------------------------------
    FormCard.FormHeader {
        visible: !page.empty
        title: i18n("Estimates")
    }
    FormCard.FormCard {
        visible: !page.empty
        FormCard.FormTextDelegate {
            text: {
                const e = page.estimates
                if (e.tasks < 3)
                    return i18n("Not enough to go on yet")
                if (e.ratio > 1.15)
                    return i18n("Things take about %1 as long as you expect", page.ratioText(e.ratio))
                if (e.ratio < 0.85)
                    return i18n("Things take less time than you expect (about %1)", page.ratioText(e.ratio))
                return i18n("Your estimates are about right")
            }
            description: page.estimates.tasks < 3
                         ? i18n("Time a few tasks through to done and this shows how long things really take compared with your estimates.")
                         : i18np("Typical result over %1 finished task with time logged.", "Typical result over %1 finished tasks with time logged.", page.estimates.tasks)
                           + (page.estimates.ratio > 1.3 ? " " + i18n("Worth padding estimates by that much when you plan.") : "")
        }
        Repeater {
            model: page.estimates.tasks >= 3 ? (page.estimates.bySize || []).filter(c => c.tasks >= 2) : []
            delegate: ColumnLayout {
                id: sizeRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.FormTextDelegate {
                    text: page.sizeText(sizeRow.modelData.size)
                    description: i18np("%1 task", "%1 tasks", sizeRow.modelData.tasks)
                    trailing: Controls.Label {
                        text: page.ratioText(sizeRow.modelData.ratio)
                        color: page.ratioColor(sizeRow.modelData.ratio)
                    }
                }
            }
        }
        Repeater {
            model: page.estimates.tasks >= 3 ? (page.estimates.byCategory || []).filter(c => c.tasks >= 2) : []
            delegate: ColumnLayout {
                id: categoryRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.FormTextDelegate {
                    text: categoryRow.modelData.name
                    description: i18np("%1 task", "%1 tasks", categoryRow.modelData.tasks)
                    trailing: Controls.Label {
                        text: page.ratioText(categoryRow.modelData.ratio)
                        color: page.ratioColor(categoryRow.modelData.ratio)
                    }
                }
            }
        }
    }

    // ---- keeps moving -------------------------------------------------------
    FormCard.FormHeader {
        visible: (page.stats.moving || []).length > 0
        title: i18n("Keeps getting moved")
    }
    FormCard.FormCard {
        visible: (page.stats.moving || []).length > 0
        FormCard.FormTextDelegate {
            description: i18n("Bumped more than once lately. A smaller first step might help, or it might be fine to let it go.")
        }
        Repeater {
            model: page.stats.moving || []
            delegate: ColumnLayout {
                id: movingRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator {}
                FormCard.FormButtonDelegate {
                    text: movingRow.modelData.title
                    description: i18np("Moved %1 time", "Moved %1 times", movingRow.modelData.bumps)
                    onClicked: page.openTask(movingRow.modelData.id)
                }
            }
        }
    }

    // ---- where the time went ------------------------------------------------
    FormCard.FormHeader {
        visible: (page.stats.tasks || []).length > 0
        title: i18n("Where the time went")
    }
    FormCard.FormCard {
        visible: (page.stats.tasks || []).length > 0
        Repeater {
            model: (page.stats.tasks || []).slice(0, page.allTasks ? undefined : 10)
            delegate: ColumnLayout {
                id: taskRow
                required property var modelData
                required property int index
                readonly property var task: modelData
                Layout.fillWidth: true
                spacing: 0
                FormCard.FormDelegateSeparator { visible: taskRow.index > 0 }
                FormCard.FormButtonDelegate {
                    readonly property var task: taskRow.task
                    text: task.title
                    description: (task.finished ? i18n("Done · estimate %1", Format.minutes(task.estimate)) : i18n("Estimate %1", Format.minutes(task.estimate)))
                                 + " · " + i18np("%1 session", "%1 sessions", task.sessions)
                    trailing: Controls.Label {
                        text: Format.minutes(taskRow.task.worked)
                        font.bold: true
                    }
                    trailingLogo.visible: false
                    onClicked: page.openTask(task.id)
                }
            }
        }
        FormCard.FormDelegateSeparator { visible: (page.stats.tasks || []).length > 10 }
        FormCard.FormButtonDelegate {
            visible: (page.stats.tasks || []).length > 10
            text: page.allTasks ? i18n("Show fewer") : i18n("Show all %1", page.stats.tasks.length)
            onClicked: page.allTasks = !page.allTasks
        }
    }
}
