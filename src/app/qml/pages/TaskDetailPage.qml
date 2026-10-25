import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// Everything about one task. Each field saves as soon as it changes, as its
// own undoable step, so there is no Save button to forget.
FormCard.FormCardPage {
    id: taskPage

    required property string taskId
    property var detail: AppController.taskDetail(taskId)
    property var proposedSteps: []
    property string stepsExplanation: ""
    property bool askingSteps: false
    readonly property bool finished: detail.status === "completed" || detail.status === "cancelled"
    readonly property bool isWorking: AppController.working.taskId === taskId
    readonly property var weekdays: [i18n("Sun"), i18n("Mon"), i18n("Tue"), i18n("Wed"), i18n("Thu"), i18n("Fri"), i18n("Sat")]

    signal openTask(string taskId)

    // Index of `value` in a list of {key: ...} entries. ComboBox.indexOfValue()
    // can't be used in bindings: it answers -1 until the model is populated.
    function indexIn(list, key, value) {
        return Math.max(0, (list || []).findIndex(entry => entry[key] === value))
    }

    title: detail.title || ""

    Connections {
        target: AppController
        function onStepsReady(id, steps, explanation) {
            if (id !== taskPage.taskId)
                return
            taskPage.askingSteps = false
            taskPage.proposedSteps = steps
            taskPage.stepsExplanation = explanation
        }
        function onAiFailed(feature) {
            if (feature === "breakdown")
                taskPage.askingSteps = false
        }
        function onChanged() {
            const d = AppController.taskDetail(taskPage.taskId)
            if (d.id === undefined) {
                // Deleted (perhaps by undo): leave.
                if (taskPage.Controls.StackView.view)
                    taskPage.Controls.StackView.view.pop()
                else
                    applicationWindow().pageStack.pop()
                return
            }
            taskPage.detail = d
        }
    }

    actions: [
        Kirigami.Action {
            text: taskPage.isWorking ? i18n("Pause") : i18n("Start")
            icon.name: taskPage.isWorking ? "media-playback-pause" : "media-playback-start"
            visible: !taskPage.finished
            onTriggered: taskPage.isWorking ? AppController.stopWork() : AppController.startWork(taskPage.taskId)
        },
        Kirigami.Action {
            text: i18n("Focus")
            icon.name: "view-fullscreen"
            visible: !taskPage.finished
            tooltip: i18n("Start and hide everything else")
            onTriggered: AppController.startFocus(taskPage.taskId)
        },
        Kirigami.Action {
            text: i18n("Schedule")
            icon.name: "view-calendar-day"
            visible: !taskPage.finished && !taskPage.detail.isRecurring
            tooltip: i18n("Put it in the next free slot")
            onTriggered: AppController.scheduleNextSlot(taskPage.taskId)
        },
        Kirigami.Action {
            text: taskPage.finished ? i18n("Reopen") : i18n("Done")
            icon.name: taskPage.finished ? "edit-undo" : "checkmark"
            onTriggered: AppController.setTaskStatus(taskPage.taskId, taskPage.finished ? "pending" : "completed")
        },
        Kirigami.Action {
            text: i18n("Delete")
            icon.name: "edit-delete"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: AppController.deleteTask(taskPage.taskId)
        }
    ]

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormTextFieldDelegate {
            label: i18n("Title")
            text: taskPage.detail.title || ""
            onEditingFinished: if (text.trim() && text !== taskPage.detail.title) AppController.updateTask(taskPage.taskId, { title: text })
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            visible: !!taskPage.detail.parentTitle
            text: i18n("Part of")
            description: taskPage.detail.parentTitle || ""
            trailing: Controls.Button {
                text: i18n("Open")
                onClicked: taskPage.openTask(taskPage.detail.parentId)
            }
        }
        FormCard.FormComboBoxDelegate {
            text: i18n("Status")
            model: [
                { value: "pending", text: i18n("To do") },
                { value: "in_progress", text: i18n("In progress") },
                { value: "completed", text: i18n("Done") },
                { value: "cancelled", text: i18n("Cancelled") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: taskPage.indexIn(model, "value", taskPage.detail.status)
            onActivated: AppController.setTaskStatus(taskPage.taskId, currentValue)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            text: i18n("Priority")
            model: [
                { value: "urgent", text: i18n("Urgent") },
                { value: "high", text: i18n("High") },
                { value: "medium", text: i18n("Medium") },
                { value: "low", text: i18n("Low") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: taskPage.indexIn(model, "value", taskPage.detail.priority)
            onActivated: AppController.updateTask(taskPage.taskId, { priority: currentValue })
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            text: i18n("Energy it takes")
            model: [
                { value: "low", text: i18n("Low: fine when tired") },
                { value: "medium", text: i18n("Medium") },
                { value: "high", text: i18n("High") },
                { value: "peak", text: i18n("Peak: needs my best hours") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: taskPage.indexIn(model, "value", taskPage.detail.energy)
            onActivated: AppController.updateTask(taskPage.taskId, { energy_required: currentValue })
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSpinBoxDelegate {
            label: i18n("Time it takes (minutes)")
            from: 5
            to: 600
            stepSize: 5
            value: taskPage.detail.estimate || 30
            onValueModified: AppController.updateTask(taskPage.taskId, { time_estimate_minutes: value })
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormDateTimeDelegate {
            text: i18n("Due")
            dateTimeDisplay: FormCard.FormDateTimeDelegate.DateTimeDisplay.Date
            resettable: true
            value: Format.parseDate(taskPage.detail.dueDate)
            onValueChanged: {
                const iso = Format.isoDate(value)
                if (iso !== (taskPage.detail.dueDate || ""))
                    AppController.updateTask(taskPage.taskId, { due_date: iso })
            }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            text: i18n("Category")
            model: AppController.categories
            textRole: "name"
            valueRole: "id"
            currentIndex: taskPage.indexIn(model, "id", taskPage.detail.categoryId)
            onActivated: AppController.updateTask(taskPage.taskId, { category_id: currentValue })
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            label: i18n("Tags")
            placeholderText: i18n("phone, errands")
            text: (taskPage.detail.tags || []).join(", ")
            onEditingFinished: {
                const tags = text.split(",").map(t => t.trim().replace(/^#/, "")).filter(t => t.length > 0)
                if (tags.join(",") !== (taskPage.detail.tags || []).join(","))
                    AppController.updateTask(taskPage.taskId, { tags: tags })
            }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextAreaDelegate {
            label: i18n("Details")
            text: taskPage.detail.description || ""
            onEditingFinished: if (text !== (taskPage.detail.description || "")) AppController.updateTask(taskPage.taskId, { description: text })
        }
    }

    // ---- Repeats -----------------------------------------------------------
    FormCard.FormHeader { title: i18n("Repeats") }
    FormCard.FormCard {
        id: repeatCard
        readonly property var rec: taskPage.detail.recurrence || null

        function save(changes) {
            const base = rec ? Object.assign({}, rec) : { type: "daily", interval: 1 }
            const next = Object.assign(base, changes)
            if (next.type === "weekly" && (!next.days || next.days.length === 0))
                next.days = [new Date().getDay()]
            AppController.updateTask(taskPage.taskId, { recurrence: next })
        }

        FormCard.FormSwitchDelegate {
            text: i18n("Repeats")
            description: taskPage.detail.recurrenceText || i18n("Just once")
            checked: repeatCard.rec !== null
            onToggled: checked ? repeatCard.save({}) : AppController.updateTask(taskPage.taskId, { recurrence: null })
        }
        FormCard.FormDelegateSeparator { visible: repeatCard.rec !== null }
        FormCard.FormComboBoxDelegate {
            visible: repeatCard.rec !== null
            text: i18n("Every")
            model: [{ value: "daily", text: i18n("Day(s)") }, { value: "weekly", text: i18n("Week(s), on chosen days") }]
            textRole: "text"
            valueRole: "value"
            currentIndex: repeatCard.rec ? taskPage.indexIn(model, "value", repeatCard.rec.type) : 0
            onActivated: repeatCard.save({ type: currentValue })
        }
        FormCard.FormSpinBoxDelegate {
            visible: repeatCard.rec !== null
            label: repeatCard.rec && repeatCard.rec.type === "weekly" ? i18n("Every how many weeks") : i18n("Every how many days")
            from: 1
            to: 52
            value: repeatCard.rec ? repeatCard.rec.interval : 1
            onValueModified: repeatCard.save({ interval: value })
        }
        FormCard.AbstractFormDelegate {
            visible: repeatCard.rec !== null && repeatCard.rec.type === "weekly"
            background: null
            contentItem: Flow {
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: 7
                    Controls.Button {
                        required property int index
                        // Monday-first display order.
                        readonly property int day: (index + 1) % 7
                        text: taskPage.weekdays[day]
                        checkable: true
                        checked: repeatCard.rec && (repeatCard.rec.days || []).indexOf(day) >= 0
                        onClicked: {
                            let days = (repeatCard.rec.days || []).slice()
                            if (checked) days.push(day)
                            else days = days.filter(d => d !== day)
                            if (days.length > 0) repeatCard.save({ days: days.sort() })
                            else checked = true
                        }
                    }
                }
            }
        }
        FormCard.FormTextFieldDelegate {
            visible: repeatCard.rec !== null
            label: i18n("At a set time (optional, HH:MM)")
            placeholderText: i18n("e.g. 08:30")
            text: taskPage.detail.scheduledTime || ""
            validator: RegularExpressionValidator { regularExpression: /^$|^([01]?\d|2[0-3]):[0-5]\d$/ }
            onEditingFinished: if (text !== (taskPage.detail.scheduledTime || ""))
                AppController.updateTask(taskPage.taskId, { scheduled_time: text, is_fixed_time: text !== "" })
        }
    }

    // ---- Steps -------------------------------------------------------------
    FormCard.FormHeader {
        title: (taskPage.detail.subtasks || []).length > 0
               ? i18n("Steps (%1 of %2 done)", taskPage.detail.subtasks.filter(s => s.status === "completed" || s.status === "cancelled").length,
                      taskPage.detail.subtasks.length)
               : i18n("Steps")
    }
    FormCard.FormCard {
        Repeater {
            model: taskPage.detail.subtasks || []
            FormCard.FormCheckDelegate {
                required property var modelData
                text: modelData.title
                checked: modelData.status === "completed"
                onToggled: AppController.setTaskStatus(modelData.id, checked ? "completed" : "pending")
                trailing: Controls.ToolButton {
                    icon.name: "go-next"
                    display: Controls.AbstractButton.IconOnly
                    text: i18n("Open")
                    onClicked: taskPage.openTask(modelData.id)
                }
            }
        }
        FormCard.FormButtonDelegate {
            visible: AppController.ai.ready && taskPage.proposedSteps.length === 0 && !taskPage.finished
            enabled: !taskPage.askingSteps
            icon.name: "tools-wizard"
            text: taskPage.askingSteps ? i18n("Asking %1…", AppController.ai.label) : i18n("Suggest steps")
            description: i18n("Break it into small steps, the first one easy to start")
            onClicked: {
                taskPage.askingSteps = true
                AppController.breakDown(taskPage.taskId)
            }
        }
        FormCard.FormSectionText {
            visible: taskPage.proposedSteps.length > 0
            text: taskPage.stepsExplanation || i18n("Suggested steps. Untick any you don't want.")
        }
        Repeater {
            model: taskPage.proposedSteps.length
            FormCard.FormCheckDelegate {
                required property int index
                readonly property var step: taskPage.proposedSteps[index]
                text: step.title
                description: Format.minutes(step.minutes) + " · " + Format.energyLabel(step.energy)
                checked: step.checked
                onToggled: {
                    const copy = taskPage.proposedSteps.slice()
                    copy[index] = Object.assign({}, copy[index], { checked: checked })
                    taskPage.proposedSteps = copy
                }
            }
        }
        FormCard.FormButtonDelegate {
            visible: taskPage.proposedSteps.length > 0
            icon.name: "list-add"
            text: i18n("Add the ticked steps")
            onClicked: {
                if (AppController.addSteps(taskPage.taskId, taskPage.proposedSteps.filter(s => s.checked)))
                    taskPage.proposedSteps = []
            }
        }
        FormCard.FormButtonDelegate {
            visible: taskPage.proposedSteps.length > 0
            text: i18n("Discard suggestions")
            onClicked: taskPage.proposedSteps = []
        }
        FormCard.FormTextFieldDelegate {
            id: newStep
            label: i18n("Add a step")
            placeholderText: i18n("Small and concrete works best")
            onAccepted: {
                if (text.trim()) {
                    AppController.addSubtask(taskPage.taskId, text)
                    clear()
                    forceActiveFocus()
                }
            }
        }
    }

    // ---- Dependencies ------------------------------------------------------
    FormCard.FormHeader { title: i18n("Waiting on") }
    FormCard.FormCard {
        Repeater {
            model: taskPage.detail.prerequisites || []
            FormCard.FormTextDelegate {
                required property var modelData
                text: modelData.title
                description: Format.statusLabel(modelData.status)
                trailing: RowLayout {
                    Controls.ToolButton {
                        icon.name: "go-next"
                        display: Controls.AbstractButton.IconOnly
                        text: i18n("Open")
                        onClicked: taskPage.openTask(modelData.id)
                    }
                    Controls.ToolButton {
                        icon.name: "list-remove"
                        display: Controls.AbstractButton.IconOnly
                        text: i18n("No longer waiting on this")
                        onClicked: AppController.removeDependency(taskPage.taskId, modelData.id)
                    }
                }
            }
        }
        FormCard.FormComboBoxDelegate {
            id: prereqPicker
            text: i18n("Add something this waits on")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: AppController.prerequisiteCandidates(taskPage.taskId)
            textRole: "title"
            valueRole: "id"
            currentIndex: -1
            onActivated: {
                AppController.addDependency(taskPage.taskId, currentValue)
                currentIndex = -1
            }
        }
        Repeater {
            model: taskPage.detail.dependents || []
            FormCard.FormTextDelegate {
                required property var modelData
                text: i18n("Holding up: %1", modelData.title)
                trailing: Controls.ToolButton {
                    icon.name: "go-next"
                    display: Controls.AbstractButton.IconOnly
                    onClicked: taskPage.openTask(modelData.id)
                }
            }
        }
    }

    // ---- Notes -------------------------------------------------------------
    FormCard.FormHeader { title: i18n("Notes") }
    FormCard.FormCard {
        id: notesCard
        property var notes: AppController.notes("", "", taskPage.taskId)
        Connections {
            target: AppController
            function onChanged(tables) { if (tables.indexOf("notes") >= 0) notesCard.notes = AppController.notes("", "", taskPage.taskId) }
        }
        Repeater {
            model: notesCard.notes
            FormCard.FormButtonDelegate {
                required property var modelData
                icon.name: "view-pim-notes"
                text: modelData.title
                description: modelData.snippet.replace(/<[^>]+>/g, "")
                onClicked: applicationWindow().openNote(modelData.id, true)
            }
        }
        FormCard.FormButtonDelegate {
            icon.name: "list-add"
            text: i18n("Add a note")
            onClicked: {
                const id = AppController.createNote({ title: taskPage.detail.title, taskId: taskPage.taskId })
                if (id)
                    applicationWindow().openNote(id, true)
            }
        }
    }

    // ---- Launchers ---------------------------------------------------------
    LauncherDialog { id: launcherDialog }
    FormCard.FormHeader { title: i18n("When I start this") }
    FormCard.FormCard {
        Repeater {
            model: taskPage.detail.actions || []
            FormCard.FormTextDelegate {
                required property var modelData
                text: modelData.label
                description: (modelData.onStart ? i18n("Opens when you start") : i18n("Only when you ask"))
                             + (modelData.needsApproval ? " · " + i18n("asks once before running") : "")
                leading: Kirigami.Icon {
                    source: modelData.icon
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: implicitWidth
                }
                trailing: RowLayout {
                    Controls.ToolButton {
                        icon.name: "media-playback-start"
                        display: Controls.AbstractButton.IconOnly
                        text: i18n("Open now")
                        onClicked: AppController.runAction(modelData.id)
                    }
                    Controls.ToolButton {
                        icon.name: "list-remove"
                        display: Controls.AbstractButton.IconOnly
                        text: i18n("Remove")
                        onClicked: AppController.removeAction(modelData.id)
                    }
                }
            }
        }
        FormCard.FormButtonDelegate {
            icon.name: "list-add"
            text: i18n("Add something to open")
            description: (taskPage.detail.actions || []).length === 0
                         ? i18n("The doc, the project, a playlist: whatever makes starting easier") : ""
            onClicked: launcherDialog.openFor(taskPage.taskId)
        }
    }

    // ---- Schedule & time ---------------------------------------------------
    FormCard.FormHeader { title: i18n("On the schedule") }
    FormCard.FormCard {
        Repeater {
            model: taskPage.detail.schedules || []
            FormCard.FormTextDelegate {
                required property var modelData
                text: modelData.label
                description: modelData.completed ? i18n("Done") : Format.minutes(Math.round((modelData.end - modelData.start) / 60000))
                trailing: Controls.ToolButton {
                    visible: !modelData.completed
                    icon.name: "list-remove"
                    display: Controls.AbstractButton.IconOnly
                    text: i18n("Remove from schedule")
                    onClicked: AppController.unschedule(modelData.id)
                }
            }
        }
        FormCard.FormButtonDelegate {
            visible: !taskPage.finished && !taskPage.detail.isRecurring
            icon.name: "view-calendar-day"
            text: i18n("Schedule in the next free slot")
            onClicked: AppController.scheduleNextSlot(taskPage.taskId)
        }
        FormCard.FormTextDelegate {
            text: i18n("Time logged")
            description: taskPage.detail.loggedMinutes > 0
                         ? i18n("%1 of an estimated %2", Format.minutes(taskPage.detail.loggedMinutes), Format.minutes(taskPage.detail.estimate))
                         : i18n("None yet")
        }
    }

    Item { Layout.preferredHeight: Kirigami.Units.gridUnit * 2 }
}
