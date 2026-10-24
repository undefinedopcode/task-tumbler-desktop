import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// "Plan my day": a proposed set of blocks to review. Nothing is scheduled
// until the user says so, and what they accept is one undoable step.
Kirigami.ScrollablePage {
    id: page

    property string day: Format.isoDate(new Date())
    property var plan: null
    property bool waiting: false
    property bool useAi: AppController.ai.ready
    property bool accepted: false

    readonly property bool isToday: day === Format.isoDate(new Date())
    readonly property var chosen: plan ? plan.blocks.filter(b => b.checked) : []

    title: i18n("Plan my day")

    function make() {
        waiting = true
        plan = null
        AppController.planDay(day, useAi && AppController.ai.ready)
    }

    function accept() {
        if (AppController.acceptPlan(chosen, plan)) {
            accepted = true
            applicationWindow().pageStack.layers.pop()
        }
    }

    Component.onCompleted: make()
    Component.onDestruction: if (plan && !accepted) AppController.dismissPlan(plan)

    Connections {
        target: AppController
        function onPlanReady(result) {
            if (result.date !== page.day)
                return
            // A copy we can tick and untick freely.
            page.plan = JSON.parse(JSON.stringify(result))
            page.waiting = false
        }
    }

    header: Controls.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Controls.ButtonGroup { id: dayGroup }
            Controls.ToolButton {
                text: i18n("Today")
                checkable: true
                checked: page.isToday
                Controls.ButtonGroup.group: dayGroup
                onClicked: { page.day = Format.isoDate(new Date()); page.make() }
            }
            Controls.ToolButton {
                text: i18n("Tomorrow")
                checkable: true
                checked: !page.isToday
                Controls.ButtonGroup.group: dayGroup
                onClicked: {
                    const d = new Date()
                    d.setDate(d.getDate() + 1)
                    page.day = Format.isoDate(d)
                    page.make()
                }
            }
            Item { Layout.fillWidth: true }
            Controls.Switch {
                visible: AppController.ai.provider !== "off"
                enabled: AppController.ai.ready && !page.waiting
                text: AppController.ai.ready ? i18n("Ask %1", AppController.ai.label) : i18n("AI not set up")
                checked: page.useAi
                onToggled: { page.useAi = checked; page.make() }
            }
            Controls.ToolButton {
                icon.name: "view-refresh"
                text: i18n("Plan again")
                enabled: !page.waiting
                onClicked: page.make()
            }
        }
    }

    footer: Controls.ToolBar {
        visible: page.plan !== null && page.plan.blocks.length > 0
        contentItem: RowLayout {
            Controls.Label {
                Layout.fillWidth: true
                text: i18np("1 block selected", "%1 blocks selected", page.chosen.length)
                color: Kirigami.Theme.disabledTextColor
            }
            Controls.Button {
                text: i18n("Not now")
                onClicked: applicationWindow().pageStack.layers.pop()
            }
            Controls.Button {
                text: i18np("Add 1 block to the schedule", "Add %1 blocks to the schedule", page.chosen.length)
                icon.name: "view-calendar-day"
                highlighted: true
                enabled: page.chosen.length > 0
                onClicked: page.accept()
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        EnergyCheckIn {
            visible: page.isToday && AppController.energy.due === true && !page.waiting
            onCheckedIn: page.make()
        }

        RowLayout {
            visible: page.waiting
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            spacing: Kirigami.Units.largeSpacing
            Controls.BusyIndicator { running: page.waiting }
            Controls.Label {
                text: page.useAi && AppController.ai.ready ? i18n("Asking %1…", AppController.ai.label) : i18n("Working it out…")
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.plan !== null && page.plan.aiError !== ""
            type: Kirigami.MessageType.Warning
            text: page.plan ? i18n("The AI couldn't help this time (%1), so this is the offline plan.", page.plan.aiError) : ""
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.plan !== null && page.plan.explanation !== ""
            type: Kirigami.MessageType.Information
            icon.name: "tools-wizard"
            text: page.plan ? page.plan.explanation + (page.plan.by ? "  (" + page.plan.by + ")" : "") : ""
        }

        Controls.Label {
            visible: page.plan !== null && page.plan.blocks.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: AppController.energy.level !== undefined && page.isToday
                  ? i18n("Fitted around your category hours, what's already scheduled, and your energy (%1).", AppController.energy.label)
                  : i18n("Fitted around your category hours and what's already scheduled. Untick anything you don't want.")
        }

        Repeater {
            model: page.plan ? page.plan.blocks.length : 0
            Controls.CheckDelegate {
                id: row
                required property int index
                readonly property var block: page.plan.blocks[index]
                Layout.fillWidth: true
                checked: block.checked
                onToggled: {
                    const p = page.plan
                    p.blocks[index].checked = checked
                    page.plan = Object.assign({}, p) // re-evaluate bindings
                }
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    ColumnLayout {
                        spacing: 0
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                        Controls.Label {
                            text: Format.time(row.block.start)
                            font.features: { "tnum": 1 }
                            font.weight: Font.DemiBold
                        }
                        Controls.Label {
                            text: Format.minutes(Math.round((row.block.end - row.block.start) / 60000))
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Controls.Label {
                            Layout.fillWidth: true
                            text: row.block.title
                            elide: Text.ElideRight
                            opacity: row.checked ? 1 : 0.5
                        }
                        Controls.Label {
                            visible: row.block.reason !== ""
                            Layout.fillWidth: true
                            text: row.block.reason
                            elide: Text.ElideRight
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }
                    }
                }
            }
        }

        SectionTitle {
            visible: page.plan !== null && page.plan.unplaced.length > 0
            text: i18n("Not in this plan")
            count: page.plan ? page.plan.unplaced.length : 0
        }
        Repeater {
            model: page.plan ? page.plan.unplaced : []
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing
                Controls.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                    text: modelData.title
                    elide: Text.ElideRight
                }
                Controls.Label {
                    Layout.fillWidth: true
                    text: modelData.reason
                    elide: Text.ElideRight
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        Kirigami.PlaceholderMessage {
            visible: page.plan !== null && page.plan.blocks.length === 0
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            icon.name: "checkmark"
            text: i18n("Nothing to add")
            explanation: page.plan && page.plan.unplaced.length > 0
                         ? i18n("Everything open is either waiting, saved for later, or doesn't fit today's hours.")
                         : i18n("Every open task already has a time, or there's nothing open.")
        }
    }
}
