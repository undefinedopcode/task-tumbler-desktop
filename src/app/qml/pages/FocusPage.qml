import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// One task, nothing else: big title, a running clock, its steps, the things
// it opens. Do Not Disturb is on while this is showing.
Kirigami.Page {
    id: page

    readonly property var working: AppController.working
    readonly property string taskId: working.taskId || lastTaskId
    property string lastTaskId: ""
    property var detail: taskId ? AppController.taskDetail(taskId) : ({})
    property double nowMs: Date.now()
    property int windowWas: Window.Windowed
    // The window this layer lives in; the global applicationWindow isn't
    // reachable yet while a layer is being created.
    readonly property var win: page.Window.window
    readonly property bool finished: detail.status === "completed"

    title: i18n("Focus")
    globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None
    padding: Kirigami.Units.gridUnit * 2

    function leave() {
        AppController.focusMode = false
    }

    onWorkingChanged: if (working.taskId) lastTaskId = working.taskId
    Component.onCompleted: lastTaskId = working.taskId || ""
    onWinChanged: if (win && windowWas === Window.Windowed) windowWas = win.visibility
    Component.onDestruction: {
        if (win && win.visibility === Window.FullScreen && windowWas !== Window.FullScreen)
            win.visibility = windowWas
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: page.nowMs = Date.now()
    }
    Connections {
        target: AppController
        function onChanged() { if (page.taskId) page.detail = AppController.taskDetail(page.taskId) }
    }
    Shortcut {
        sequences: ["Escape"]
        onActivated: page.leave()
    }

    background: Rectangle { color: Kirigami.Theme.backgroundColor }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width, Kirigami.Units.gridUnit * 40)
        spacing: Kirigami.Units.gridUnit

        RowLayout {
            Layout.fillWidth: true
            Controls.Label {
                text: AppController.working.taskId ? i18n("Focusing") : (page.finished ? i18n("Done") : i18n("Paused"))
                color: AppController.working.taskId ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Controls.ToolButton {
                flat: true
                readonly property bool full: page.win !== null && page.win.visibility === Window.FullScreen
                icon.name: full ? "view-restore" : "view-fullscreen"
                text: full ? i18n("Exit full screen") : i18n("Full screen")
                onClicked: if (page.win) page.win.visibility = full ? page.windowWas : Window.FullScreen
            }
            Controls.ToolButton {
                flat: true
                icon.name: "window-close"
                text: i18n("Leave focus (Esc)")
                onClicked: page.leave()
            }
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 1
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 2.2
            wrapMode: Text.Wrap
            text: page.detail.title || i18n("Nothing to focus on")
        }

        Controls.Label {
            visible: AppController.working.startedAt !== undefined
            text: AppController.working.startedAt ? Format.elapsed(page.nowMs - AppController.working.startedAt) : ""
            font.pixelSize: Kirigami.Units.gridUnit * 4.5
            font.weight: Font.Light
            font.features: { "tnum": 1 }
        }
        Controls.Label {
            visible: !!page.detail.estimate && AppController.working.startedAt !== undefined
            text: page.detail.estimate ? i18n("Estimated %1", Format.minutes(page.detail.estimate)) : ""
            color: Kirigami.Theme.disabledTextColor
        }

        // Steps: tick them off without leaving focus.
        ColumnLayout {
            Layout.fillWidth: true
            visible: (page.detail.subtasks || []).length > 0
            spacing: 0
            Repeater {
                model: page.detail.subtasks || []
                Controls.CheckBox {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.title
                    checked: modelData.status === "completed"
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                    onToggled: AppController.setTaskStatus(modelData.id, checked ? "completed" : "pending")
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            visible: (page.detail.actions || []).length > 0
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                model: page.detail.actions || []
                Controls.Button {
                    required property var modelData
                    text: modelData.label
                    icon.name: modelData.icon
                    onClicked: AppController.runAction(modelData.id)
                }
            }
        }

        Controls.Label {
            visible: !!page.detail.description
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: page.detail.description || ""
            color: Kirigami.Theme.disabledTextColor
        }

        RowLayout {
            Layout.topMargin: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.largeSpacing
            Controls.Button {
                visible: !page.finished
                text: i18n("Done")
                icon.name: "checkmark"
                highlighted: true
                onClicked: AppController.setTaskStatus(page.taskId, "completed")
            }
            Controls.Button {
                visible: AppController.working.taskId !== undefined
                text: i18n("Pause")
                icon.name: "media-playback-pause"
                onClicked: AppController.stopWork()
            }
            Controls.Button {
                visible: AppController.working.taskId === undefined && !page.finished && page.taskId !== ""
                text: i18n("Resume")
                icon.name: "media-playback-start"
                onClicked: AppController.startWork(page.taskId)
            }
            Controls.Button {
                visible: page.finished
                text: i18n("Plan the next thing")
                icon.name: "view-calendar-week"
                onClicked: {
                    const w = page.win
                    page.leave()
                    if (w)
                        w.openPlan()
                }
            }
        }
    }
}
