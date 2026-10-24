import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

// Create or edit an event, including what may be scheduled during it.
FormCard.FormCardDialog {
    id: eventDialog

    property var event: ({})
    readonly property var swatches: ["#6366f1", "#0ea5e9", "#10b981", "#f59e0b", "#ef4444", "#ec4899", "#a855f7", "#64748b"]
    readonly property var modes: [
        { value: "none", text: i18n("Anything can be scheduled") },
        { value: "block_all", text: i18n("Nothing else during it") },
        { value: "recurring_only", text: i18n("Only routines") },
        { value: "tags_only", text: i18n("Only tasks with certain tags") }
    ]

    // `start` (ms) pre-fills a new event at that time.
    function edit(id, start) {
        if (id) {
            event = AppController.eventDetail(id)
        } else {
            const s = start ? new Date(start) : new Date()
            if (!start) {
                s.setMinutes(0, 0, 0)
                s.setHours(s.getHours() + 1)
            }
            const e = new Date(s.getTime() + 3600000)
            event = {
                id: "", title: "", allDay: false, color: swatches[0], blockingMode: "block_all", allowedTags: [], location: "",
                date: Format.isoDate(s), endDate: Format.isoDate(e),
                startTime: Qt.formatTime(s, "HH:mm"), endTime: Qt.formatTime(e, "HH:mm")
            }
        }
        titleField.text = event.title
        tagsField.text = (event.allowedTags || []).join(", ")
        locationField.text = event.location || ""
        open()
        titleField.forceActiveFocus()
    }

    function combine(isoDate, hhmm) {
        const d = Format.parseDate(isoDate)
        const p = (hhmm || "09:00").split(":")
        d.setHours(parseInt(p[0]), parseInt(p[1]), 0, 0)
        return d
    }

    title: event.id ? i18n("Edit event") : i18n("New event")
    standardButtons: Controls.Dialog.Save | Controls.Dialog.Cancel

    onAccepted: {
        const e = Object.assign({}, event, {
            title: titleField.text,
            location: locationField.text,
            allowedTags: tagsField.text.split(",").map(t => t.trim().replace(/^#/, "")).filter(t => t.length > 0)
        })
        AppController.saveEvent(e)
    }

    FormCard.FormTextFieldDelegate {
        id: titleField
        label: i18n("What")
        placeholderText: i18n("Physio, school run, flight…")
    }
    FormCard.FormDelegateSeparator {}
    FormCard.FormSwitchDelegate {
        text: i18n("All day")
        checked: !!eventDialog.event.allDay
        onToggled: eventDialog.event = Object.assign({}, eventDialog.event, { allDay: checked })
    }
    FormCard.FormDelegateSeparator {}
    FormCard.FormDateTimeDelegate {
        text: i18n("Date")
        dateTimeDisplay: FormCard.FormDateTimeDelegate.DateTimeDisplay.Date
        value: Format.parseDate(eventDialog.event.date)
        onValueChanged: {
            const iso = Format.isoDate(value)
            if (iso && iso !== eventDialog.event.date) {
                // Moving the start date moves the end date with it.
                const shift = Format.parseDate(eventDialog.event.endDate) - Format.parseDate(eventDialog.event.date)
                const end = new Date(value.getTime() + (isNaN(shift) ? 0 : shift))
                eventDialog.event = Object.assign({}, eventDialog.event, { date: iso, endDate: Format.isoDate(end) })
            }
        }
    }
    FormCard.FormDateTimeDelegate {
        visible: !eventDialog.event.allDay
        text: i18n("Starts")
        dateTimeDisplay: FormCard.FormDateTimeDelegate.DateTimeDisplay.Time
        value: eventDialog.combine(eventDialog.event.date, eventDialog.event.startTime)
        onValueChanged: {
            const t = Qt.formatTime(value, "HH:mm")
            if (t && t !== eventDialog.event.startTime)
                eventDialog.event = Object.assign({}, eventDialog.event, { startTime: t })
        }
    }
    FormCard.FormDateTimeDelegate {
        visible: !eventDialog.event.allDay
        text: i18n("Ends")
        dateTimeDisplay: FormCard.FormDateTimeDelegate.DateTimeDisplay.Time
        value: eventDialog.combine(eventDialog.event.date, eventDialog.event.endTime)
        onValueChanged: {
            const t = Qt.formatTime(value, "HH:mm")
            if (t && t !== eventDialog.event.endTime)
                eventDialog.event = Object.assign({}, eventDialog.event, { endTime: t, endDate: eventDialog.event.date })
        }
    }
    FormCard.FormDateTimeDelegate {
        visible: !!eventDialog.event.allDay
        text: i18n("Until")
        dateTimeDisplay: FormCard.FormDateTimeDelegate.DateTimeDisplay.Date
        value: Format.parseDate(eventDialog.event.endDate)
        onValueChanged: {
            const iso = Format.isoDate(value)
            if (iso && iso !== eventDialog.event.endDate)
                eventDialog.event = Object.assign({}, eventDialog.event, { endDate: iso })
        }
    }
    FormCard.FormDelegateSeparator {}
    FormCard.FormComboBoxDelegate {
        text: i18n("During it")
        model: eventDialog.modes
        textRole: "text"
        valueRole: "value"
        currentIndex: Math.max(0, eventDialog.modes.findIndex(m => m.value === eventDialog.event.blockingMode))
        onActivated: eventDialog.event = Object.assign({}, eventDialog.event, { blockingMode: currentValue })
    }
    FormCard.FormTextFieldDelegate {
        id: tagsField
        visible: eventDialog.event.blockingMode === "tags_only"
        label: i18n("Allowed tags")
        placeholderText: i18n("phone, reading")
    }
    FormCard.FormDelegateSeparator {}
    FormCard.FormTextFieldDelegate {
        id: locationField
        label: i18n("Where (optional)")
    }
    FormCard.AbstractFormDelegate {
        background: null
        contentItem: Flow {
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                model: eventDialog.swatches
                Rectangle {
                    required property string modelData
                    width: Kirigami.Units.gridUnit * 1.3
                    height: width
                    radius: width / 2
                    color: modelData
                    border.width: eventDialog.event.color === modelData ? 3 : 0
                    border.color: Kirigami.Theme.textColor
                    TapHandler { onTapped: eventDialog.event = Object.assign({}, eventDialog.event, { color: parent.modelData }) }
                }
            }
        }
    }
    FormCard.FormButtonDelegate {
        visible: !!eventDialog.event.id
        text: i18n("Delete event")
        icon.name: "edit-delete"
        onClicked: {
            AppController.deleteEvent(eventDialog.event.id)
            eventDialog.close()
        }
    }
}
