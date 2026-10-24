import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// One day as a timeline. Category hours shade the background, blocks can be
// dragged to a new time (snapping to 15 minutes), and every block has the
// same actions as on Today.
Kirigami.Page {
    id: page

    property date day: new Date()
    readonly property string isoDay: Format.isoDate(day)
    property var agenda: AppController.dayAgenda(isoDay)
    readonly property real hourHeight: Kirigami.Units.gridUnit * 3.2
    readonly property real pxPerMs: hourHeight / 3600000
    property double nowMs: Date.now()

    signal openTask(string taskId)

    title: agenda.label || i18n("Schedule")
    padding: 0

    function shiftDay(days) {
        const d = new Date(day)
        d.setDate(d.getDate() + days)
        day = d
    }
    function yFor(ms) { return (ms - agenda.dayStart) * pxPerMs }

    // Side-by-side lanes for overlapping items: each item takes the first
    // free lane, and every item in an overlapping cluster shares its width.
    readonly property var laidOut: {
        const items = (agenda.items || []).filter(i => !i.timeless)
            .map(i => Object.assign({}, i))
            .sort((a, b) => a.start - b.start || b.end - a.end)
        let cluster = [], laneEnds = [], clusterEnd = 0
        const close = () => { cluster.forEach(i => i.lanes = laneEnds.length); cluster = []; laneEnds = [] }
        for (const item of items) {
            // Short items still occupy their drawn minimum height.
            const end = Math.max(item.end, item.start + 30 * 60000)
            if (cluster.length && item.start >= clusterEnd)
                close()
            let lane = laneEnds.findIndex(e => e <= item.start)
            if (lane < 0) { lane = laneEnds.length; laneEnds.push(end) } else laneEnds[lane] = end
            item.lane = lane
            cluster.push(item)
            clusterEnd = cluster.length === 1 ? end : Math.max(clusterEnd, end)
        }
        close()
        return items
    }

    onIsoDayChanged: {
        agenda = AppController.dayAgenda(isoDay)
        scroller.scrollToStart()
    }
    Connections {
        target: AppController
        function onChanged() { page.agenda = AppController.dayAgenda(page.isoDay) }
        function onTodayChanged() { page.nowMs = Date.now() }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Add event")
            icon.name: "appointment-new"
            onTriggered: {
                // The next whole hour today; 09:00 on other days.
                const start = new Date(page.day)
                if (page.agenda.isToday) {
                    const now = new Date()
                    start.setHours(now.getHours() + 1, 0, 0, 0)
                } else {
                    start.setHours(9, 0, 0, 0)
                }
                applicationWindow().editEvent("", start.getTime())
            }
        },
        Kirigami.Action {
            icon.name: "go-previous"
            text: i18n("Previous day")
            displayHint: Kirigami.DisplayHint.IconOnly
            shortcut: "Alt+Left"
            onTriggered: page.shiftDay(-1)
        },
        Kirigami.Action {
            text: i18n("Today")
            enabled: !page.agenda.isToday
            onTriggered: page.day = new Date()
        },
        Kirigami.Action {
            icon.name: "go-next"
            text: i18n("Next day")
            displayHint: Kirigami.DisplayHint.IconOnly
            shortcut: "Alt+Right"
            onTriggered: page.shiftDay(1)
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Flow {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: page.agenda.allDay && page.agenda.allDay.length > 0
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                model: page.agenda.allDay || []
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

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: canvas.height
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            // Start an hour before now, or at 7:00 on other days.
            function scrollToStart() {
                const anchor = page.agenda.isToday ? page.yFor(Date.now()) - page.hourHeight : page.hourHeight * 7
                contentY = Math.max(0, Math.min(anchor, contentHeight - height))
            }
            onHeightChanged: if (height > 0 && !positioned) { scrollToStart(); positioned = true }
            property bool positioned: false

            Item {
                id: canvas
                width: scroller.width - Kirigami.Units.largeSpacing
                height: page.hourHeight * 24 + Kirigami.Units.largeSpacing
                readonly property real gutter: Kirigami.Units.gridUnit * 4
                readonly property real laneWidth: width - gutter - Kirigami.Units.largeSpacing

                // Category hours.
                Repeater {
                    model: page.agenda.windows || []
                    Rectangle {
                        required property var modelData
                        x: canvas.gutter
                        width: canvas.laneWidth
                        y: page.yFor(modelData.start)
                        height: (modelData.end - modelData.start) * page.pxPerMs
                        color: Qt.alpha(modelData.color || Kirigami.Theme.highlightColor, 0.06)
                        Rectangle {
                            width: 3
                            height: parent.height
                            color: Qt.alpha(parent.modelData.color || Kirigami.Theme.highlightColor, 0.5)
                        }
                        Controls.Label {
                            anchors.right: parent.right
                            anchors.rightMargin: Kirigami.Units.smallSpacing
                            y: Kirigami.Units.smallSpacing
                            text: parent.modelData.name
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }

                // Hour lines and labels.
                Repeater {
                    model: 24
                    Item {
                        required property int index
                        y: index * page.hourHeight
                        width: canvas.width
                        height: page.hourHeight
                        Controls.Label {
                            x: Kirigami.Units.smallSpacing
                            y: -height / 2
                            width: canvas.gutter - Kirigami.Units.largeSpacing
                            horizontalAlignment: Text.AlignRight
                            visible: parent.index > 0
                            text: Qt.formatTime(new Date(2000, 0, 1, parent.index, 0), Qt.locale().timeFormat(Locale.ShortFormat))
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }
                        Kirigami.Separator {
                            x: canvas.gutter
                            width: canvas.laneWidth
                            opacity: 0.6
                        }
                    }
                }

                // Blocks, recurring items and events.
                Repeater {
                    model: page.laidOut
                    delegate: Rectangle {
                        id: block
                        required property var modelData
                        readonly property bool draggable: modelData.kind === "schedule" && modelData.state !== "done"
                        readonly property bool finished: modelData.state === "done" || modelData.state === "skipped"
                        readonly property color tint: modelData.kind === "event" ? (modelData.color || Kirigami.Theme.disabledTextColor)
                                                      : modelData.state === "overdue" ? Format.priorityColors.urgent
                                                      : Format.priorityColors[modelData.priority]

                        readonly property real lanePx: (canvas.laneWidth - Kirigami.Units.smallSpacing) / modelData.lanes
                        x: canvas.gutter + Kirigami.Units.smallSpacing + modelData.lane * lanePx
                        width: lanePx - Kirigami.Units.smallSpacing
                        y: page.yFor(modelData.start)
                        height: Math.max(Kirigami.Units.gridUnit * 1.4, (modelData.end - modelData.start) * page.pxPerMs - 2)
                        radius: Kirigami.Units.cornerRadius
                        color: Qt.alpha(tint, finished ? 0.10 : 0.22)
                        border.color: Qt.alpha(tint, 0.8)
                        border.width: modelData.working ? 2 : 1
                        opacity: finished ? 0.6 : 1
                        z: drag.active ? 10 : 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: 0
                            Controls.Label {
                                Layout.fillWidth: true
                                text: block.modelData.title
                                elide: Text.ElideRight
                                font.weight: Font.DemiBold
                                font.strikeout: block.modelData.state === "done"
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                visible: block.height > Kirigami.Units.gridUnit * 2.4
                                text: drag.active ? i18n("Drop at %1", Format.time(block.dropTime()))
                                                  : Format.timeRange(block.modelData.start, block.modelData.end)
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                            }
                        }

                        // Snap a dragged block to 15 minutes from the top of the day.
                        function dropTime() {
                            const quarter = 15 * 60000
                            const ms = page.agenda.dayStart + y / page.pxPerMs
                            return page.agenda.dayStart + Math.round((ms - page.agenda.dayStart) / quarter) * quarter
                        }

                        DragHandler {
                            id: drag
                            enabled: block.draggable
                            xAxis.enabled: false
                            yAxis.minimum: 0
                            yAxis.maximum: canvas.height - block.height
                            onActiveChanged: {
                                if (!active) {
                                    const t = block.dropTime()
                                    if (t !== block.modelData.start)
                                        AppController.moveSchedule(block.modelData.id, t)
                                    else
                                        block.y = Qt.binding(() => page.yFor(block.modelData.start))
                                }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onTapped: (point, button) => {
                                if (block.modelData.kind === "event")
                                    applicationWindow().editEvent(block.modelData.id)
                                else
                                    blockMenu.popup()
                            }
                        }

                        Controls.Menu {
                            id: blockMenu
                            Controls.MenuItem {
                                visible: block.modelData.kind === "schedule" && !block.finished
                                height: visible ? implicitHeight : 0
                                text: i18n("Start working")
                                icon.name: "media-playback-start"
                                onTriggered: AppController.startWork(block.modelData.taskId)
                            }
                            Controls.MenuItem {
                                visible: !block.finished
                                height: visible ? implicitHeight : 0
                                text: i18n("Done")
                                icon.name: "checkmark"
                                onTriggered: block.modelData.kind === "schedule" ? AppController.completeSchedule(block.modelData.id)
                                                                                 : AppController.setOccurrence(block.modelData.id, "completed")
                            }
                            Controls.MenuItem {
                                visible: block.modelData.kind === "schedule" && !block.finished
                                height: visible ? implicitHeight : 0
                                text: i18n("Next free slot")
                                icon.name: "go-next"
                                onTriggered: AppController.bumpToNextSlot(block.modelData.id)
                            }
                            Controls.MenuItem {
                                visible: block.modelData.kind === "schedule" && !block.finished
                                height: visible ? implicitHeight : 0
                                text: i18n("Tomorrow")
                                icon.name: "go-next-skip"
                                onTriggered: AppController.bumpToTomorrow(block.modelData.id)
                            }
                            Controls.MenuItem {
                                visible: block.modelData.kind === "occurrence" && !block.finished
                                height: visible ? implicitHeight : 0
                                text: i18n("Skip")
                                icon.name: "go-next-skip"
                                onTriggered: AppController.setOccurrence(block.modelData.id, "skipped")
                            }
                            Controls.MenuItem {
                                visible: block.modelData.kind === "schedule"
                                height: visible ? implicitHeight : 0
                                text: i18n("Remove from schedule")
                                icon.name: "edit-delete-remove"
                                onTriggered: AppController.unschedule(block.modelData.id)
                            }
                            Controls.MenuItem {
                                text: i18n("Open task")
                                icon.name: "document-open"
                                onTriggered: page.openTask(block.modelData.taskId)
                            }
                        }
                    }
                }

                // The current time.
                Rectangle {
                    visible: page.agenda.isToday === true
                    x: canvas.gutter - Kirigami.Units.smallSpacing
                    width: canvas.laneWidth + Kirigami.Units.smallSpacing
                    y: page.yFor(page.nowMs)
                    height: 2
                    color: Format.priorityColors.urgent
                    z: 20
                    Rectangle {
                        x: -width / 2
                        y: -height / 2 + 1
                        width: Kirigami.Units.smallSpacing * 2
                        height: width
                        radius: width / 2
                        color: parent.color
                    }
                }
            }

        }

        // Recurring items with no time of day.
        Controls.ToolBar {
            id: anytimeBar
            Layout.fillWidth: true
            readonly property var anytime: (page.agenda.items || []).filter(i => i.timeless)
            visible: anytime.length > 0
            position: Controls.ToolBar.Footer
            contentItem: Flow {
                spacing: Kirigami.Units.smallSpacing
                Controls.Label {
                    text: i18n("Any time today:")
                    color: Kirigami.Theme.disabledTextColor
                }
                Repeater {
                    model: anytimeBar.anytime
                    Controls.CheckBox {
                        required property var modelData
                        text: modelData.title
                        checked: modelData.state === "done"
                        onToggled: AppController.setOccurrence(modelData.id, checked ? "completed" : "pending")
                    }
                }
            }
        }
    }
}
