import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.tasktumbler

FormCard.FormCardPage {
    id: page

    readonly property var s: AppController.settings
    property string importReport: ""

    title: i18n("Settings")

    FormCard.FormHeader { title: i18n("Reminders") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Desktop notifications")
            description: i18n("Before and at the start of each block, with buttons to start, bump or finish it")
            checked: page.s.notifications
            onToggled: AppController.setSetting("notifications", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSpinBoxDelegate {
            label: i18n("Heads-up before a block starts (minutes, 0 for none)")
            from: 0
            to: 60
            value: page.s.reminderMinutes
            enabled: page.s.notifications
            onValueModified: AppController.setSetting("reminder_minutes", value)
        }
    }

    FormCard.FormHeader { title: i18n("Window") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Keep running in the system tray when closed")
            description: i18n("Reminders only work while Task Tumbler is running")
            checked: page.s.closeToTray
            onToggled: AppController.setSetting("close_to_tray", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSwitchDelegate {
            text: i18n("Start at login")
            description: i18n("Starts quietly in the tray")
            checked: page.s.autostart
            onToggled: AppController.setSetting("autostart", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("Quick capture from anywhere")
            description: i18n("Meta+Shift+Space by default; change it in System Settings → Shortcuts → Task Tumbler")
        }
    }

    FormCard.FormHeader { title: i18n("Energy") }
    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            label: i18n("Best hours start (HH:MM)")
            text: page.s.peakEnergyStart
            validator: RegularExpressionValidator { regularExpression: /^([01]?\d|2[0-3]):[0-5]\d$/ }
            onEditingFinished: if (acceptableInput && text !== page.s.peakEnergyStart) AppController.setSetting("peak_energy_start", text)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            label: i18n("Best hours end (HH:MM)")
            text: page.s.peakEnergyEnd
            validator: RegularExpressionValidator { regularExpression: /^([01]?\d|2[0-3]):[0-5]\d$/ }
            onEditingFinished: if (acceptableInput && text !== page.s.peakEnergyEnd) AppController.setSetting("peak_energy_end", text)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextAreaDelegate {
            label: i18n("Notes for the AI scheduler")
            description: i18n("Used once AI scheduling arrives")
            text: page.s.aiHints
            onEditingFinished: if (text !== page.s.aiHints) AppController.setSetting("ai_scheduling_hints", text)
        }
    }

    FormCard.FormHeader { title: i18n("Your data") }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18n("Stored on this computer")
            description: AppController.dataPath
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            text: i18n("Back up now")
            description: i18n("A copy is also made automatically once a day; the last 14 are kept")
            icon.name: "document-save"
            onClicked: AppController.backupNow()
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            text: i18n("Open data folder")
            icon.name: "folder-open"
            onClicked: AppController.openDataFolder()
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            text: i18n("Import from the Task Tumbler web app…")
            description: i18n("Only into an empty database. Export with: npm run export-desktop")
            icon.name: "document-import"
            onClicked: importDialog.open()
        }
        FormCard.FormSectionText {
            visible: page.importReport !== ""
            text: page.importReport
        }
    }

    FileDialog {
        id: importDialog
        title: i18n("Choose a Task Tumbler export")
        nameFilters: [i18n("Task Tumbler export (*.json)")]
        onAccepted: page.importReport = AppController.importExport(selectedFile)
    }
}
