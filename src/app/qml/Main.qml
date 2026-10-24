import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

Kirigami.ApplicationWindow {
    id: root

    property string currentPage: ""

    title: i18n("Task Tumbler")
    width: Kirigami.Units.gridUnit * 62
    height: Kirigami.Units.gridUnit * 42
    minimumWidth: Kirigami.Units.gridUnit * 22
    minimumHeight: Kirigami.Units.gridUnit * 20
    visible: false

    function showPage(name) {
        const pages = {
            today: todayPage,
            tasks: tasksPage,
            schedule: schedulePage,
            categories: categoriesPage,
            settings: settingsPage,
            onboarding: onboardingPage,
            energy: energyPage
        }
        if (name === "plan") {
            showPage("today")
            openPlan()
            return
        }
        if (name === "capture") {
            openCapture()
            return
        }
        if (name.startsWith("event:")) {
            showPage("schedule")
            editEvent(name === "event:new" ? "" : name.slice(6))
            return
        }
        if (name.startsWith("task:")) {
            showPage("tasks")
            openTask(name.slice(5))
            return
        }
        if (!pages[name])
            return
        currentPage = name
        pageStack.clear()
        pageStack.push(pages[name])
    }

    function openTask(taskId) {
        if (!taskId)
            return
        // Replace a detail page already open rather than stacking them up.
        while (pageStack.depth > 1)
            pageStack.pop()
        pageStack.push(taskDetailPage, { taskId: taskId })
    }

    function openCapture() {
        captureWindow.openCapture()
    }

    // A layer, not a column: it's a short side trip that covers the page
    // it came from, and pops back when done.
    function openPlan() {
        if (pageStack.layers.depth > 1)
            return
        pageStack.layers.push(planPage)
    }

    // An existing event by id, or a new one starting at `start` (ms).
    function editEvent(id, start) {
        eventDialog.edit(id, start)
    }

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: false
        modal: !root.wideScreen
        collapsible: true
        collapsed: !root.wideScreen
        title: i18n("Task Tumbler")
        titleIcon: "qrc:/icons/org.kde.tasktumbler.svg"

        actions: [
            Kirigami.Action {
                text: i18n("Today")
                icon.name: "view-calendar-day"
                checked: root.currentPage === "today"
                shortcut: "Ctrl+1"
                onTriggered: root.showPage("today")
            },
            Kirigami.Action {
                text: i18n("Tasks")
                icon.name: "view-task"
                checked: root.currentPage === "tasks"
                shortcut: "Ctrl+2"
                onTriggered: root.showPage("tasks")
            },
            Kirigami.Action {
                text: i18n("Schedule")
                icon.name: "view-calendar-list"
                checked: root.currentPage === "schedule"
                shortcut: "Ctrl+3"
                onTriggered: root.showPage("schedule")
            },
            Kirigami.Action {
                text: i18n("Energy")
                icon.name: "battery-060"
                checked: root.currentPage === "energy"
                shortcut: "Ctrl+5"
                onTriggered: root.showPage("energy")
            },
            Kirigami.Action {
                text: i18n("Categories")
                icon.name: "tag"
                checked: root.currentPage === "categories"
                shortcut: "Ctrl+4"
                onTriggered: root.showPage("categories")
            },
            Kirigami.Action { separator: true },
            Kirigami.Action {
                text: i18n("Plan My Day")
                icon.name: "view-calendar-week"
                shortcut: "Ctrl+P"
                onTriggered: root.openPlan()
            },
            Kirigami.Action {
                text: i18n("Quick Capture")
                icon.name: "list-add"
                onTriggered: root.openCapture()
            },
            Kirigami.Action {
                text: AppController.canUndo ? i18n("Undo: %1", AppController.undoText) : i18n("Undo")
                icon.name: "edit-undo"
                enabled: AppController.canUndo
                onTriggered: AppController.undo()
            },
            Kirigami.Action { separator: true },
            Kirigami.Action {
                text: i18n("Settings")
                icon.name: "settings-configure"
                checked: root.currentPage === "settings"
                shortcut: "Ctrl+,"
                onTriggered: root.showPage("settings")
            },
            Kirigami.Action {
                text: i18n("Quit")
                icon.name: "application-exit"
                shortcut: StandardKey.Quit
                onTriggered: Qt.quit()
            }
        ]
    }

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.defaultColumnWidth: Kirigami.Units.gridUnit * 30

    Component.onCompleted: showPage(AppController.needsOnboarding ? "onboarding" : "today")

    Connections {
        target: AppController
        function onMessage(text) { root.showPassiveNotification(text, "short") }
        function onShowCaptureRequested() { root.openCapture() }
        function onShowWindowRequested() {
            root.show()
            root.raise()
            root.requestActivate()
        }
        function onOnboardingChanged() {
            if (!AppController.needsOnboarding && root.currentPage === "onboarding")
                root.showPage("today")
        }
    }

    Shortcut {
        sequences: ["Ctrl+N", "Ctrl+K"]
        onActivated: root.openCapture()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: AppController.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Y"]
        onActivated: AppController.redo()
    }

    onClosing: close => {
        if (AppController.settings.closeToTray) {
            close.accepted = false
            root.hide()
        }
    }

    QuickCaptureWindow {
        id: captureWindow
    }

    EventDialog {
        id: eventDialog
    }

    Component {
        id: todayPage
        TodayPage { onOpenTask: taskId => root.openTask(taskId) }
    }
    Component {
        id: tasksPage
        TasksPage { onOpenTask: taskId => root.openTask(taskId) }
    }
    Component {
        id: schedulePage
        SchedulePage { onOpenTask: taskId => root.openTask(taskId) }
    }
    Component {
        id: categoriesPage
        CategoriesPage {}
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
    Component {
        id: onboardingPage
        OnboardingPage {}
    }
    Component {
        id: planPage
        PlanPage {}
    }
    Component {
        id: energyPage
        EnergyPage {}
    }
    Component {
        id: taskDetailPage
        TaskDetailPage { onOpenTask: taskId => root.openTask(taskId) }
    }
}
