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
            energy: energyPage,
            notes: notesPage
        }
        if (name === "unfocus") {
            AppController.focusMode = false
            return
        }
        if (name === "focus") {
            showPage("today")
            AppController.focusMode = true
            return
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
        if (name.startsWith("note:")) {
            showPage("notes")
            openNote(name.slice(5))
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

    // From the notes list: replaces any open note. From a task: opens beside it.
    function openNote(noteId, besideTask) {
        if (!noteId)
            return
        while (pageStack.depth > (besideTask ? 2 : 1))
            pageStack.pop()
        pageStack.push(noteEditorPage, { noteId: noteId })
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

    function openFocus() {
        if (pageStack.layers.currentItem && pageStack.layers.currentItem.objectName === "focus")
            return
        pageStack.layers.push(focusPage)
    }

    // An existing event by id, or a new one starting at `start` (ms).
    function editEvent(id, start) {
        eventDialog.edit(id, start)
    }

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: false
        // Tucked away while focusing: nothing but the task.
        modal: !root.wideScreen || AppController.focusMode
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
                text: i18n("Notes")
                icon.name: "view-pim-notes"
                checked: root.currentPage === "notes"
                shortcut: "Ctrl+6"
                onTriggered: root.showPage("notes")
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
        function onOpenTaskRequested(taskId) { root.openTask(taskId) }
        function onFocusModeChanged() {
            if (AppController.focusMode) {
                root.show()
                root.raise()
                root.globalDrawer.close()
                root.openFocus()
            } else {
                if (root.pageStack.layers.currentItem && root.pageStack.layers.currentItem.objectName === "focus")
                    root.pageStack.layers.pop()
                if (root.wideScreen)
                    root.globalDrawer.open()
            }
        }
        function onCommandNeedsApproval(actionId, taskTitle, command) {
            approval.actionId = actionId
            approval.taskTitle = taskTitle
            approval.command = command
            approval.open()
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

    // A launcher command runs only after this, once per exact command.
    Kirigami.PromptDialog {
        id: approval
        property string actionId
        property string taskTitle
        property string command
        title: i18n("Run this command?")
        subtitle: i18n("“%1” wants to run:", taskTitle)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Allow and run")
                icon.name: "dialog-ok"
                onTriggered: {
                    AppController.approveCommand(approval.actionId)
                    approval.close()
                }
            },
            Kirigami.Action {
                text: i18n("Not now")
                icon.name: "dialog-cancel"
                onTriggered: approval.close()
            }
        ]
        Controls.Label {
            text: approval.command
            font.family: "monospace"
            wrapMode: Text.WrapAnywhere
        }
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
        id: focusPage
        FocusPage {
            objectName: "focus"
            Component.onDestruction: AppController.focusMode = false
        }
    }
    Component {
        id: energyPage
        EnergyPage {}
    }
    Component {
        id: notesPage
        NotesPage { onOpenNote: noteId => root.openNote(noteId) }
    }
    Component {
        id: noteEditorPage
        NoteEditorPage { onOpenTask: taskId => root.openTask(taskId) }
    }
    Component {
        id: taskDetailPage
        TaskDetailPage { onOpenTask: taskId => root.openTask(taskId) }
    }
}
