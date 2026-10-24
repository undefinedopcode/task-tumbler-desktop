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

    FormCard.FormHeader { title: i18n("AI") }
    FormCard.FormCard {
        id: aiCard
        readonly property var a: AppController.ai

        FormCard.FormComboBoxDelegate {
            text: i18n("Use")
            description: i18n("Everything works without AI. With it, plans and steps get smarter.")
            model: [
                { value: "off", text: i18n("No AI") },
                { value: "anthropic", text: i18n("Claude (Anthropic API key)") },
                { value: "ollama", text: i18n("A local model (Ollama)") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: Math.max(0, model.findIndex(m => m.value === aiCard.a.provider))
            onActivated: {
                AppController.setSetting("ai_provider", currentValue)
                if (currentValue === "ollama")
                    AppController.refreshOllamaModels()
            }
        }

        // Claude
        FormCard.FormDelegateSeparator { visible: aiCard.a.provider === "anthropic" }
        FormCard.FormPasswordFieldDelegate {
            id: keyField
            visible: aiCard.a.provider === "anthropic"
            label: aiCard.a.hasKey ? i18n("API key (saved in your keyring; type to replace)") : i18n("API key")
            placeholderText: "sk-ant-…"
            onAccepted: { AppController.setApiKey(text); text = "" }
        }
        FormCard.FormButtonDelegate {
            visible: aiCard.a.provider === "anthropic" && keyField.text !== ""
            text: i18n("Save key")
            icon.name: "document-save"
            onClicked: { AppController.setApiKey(keyField.text); keyField.text = "" }
        }
        FormCard.FormTextFieldDelegate {
            visible: aiCard.a.provider === "anthropic"
            label: i18n("Model")
            text: aiCard.a.model
            onEditingFinished: if (text.trim() && text !== aiCard.a.model) AppController.setSetting("ai_model", text.trim())
        }
        FormCard.FormSwitchDelegate {
            visible: aiCard.a.provider === "anthropic"
            text: i18n("Include task notes")
            description: i18n("Off: only titles, times and priorities leave this computer")
            checked: aiCard.a.shareNotes
            onToggled: AppController.setSetting("ai_share_notes", checked)
        }

        // Ollama
        FormCard.FormDelegateSeparator { visible: aiCard.a.provider === "ollama" }
        FormCard.FormTextFieldDelegate {
            visible: aiCard.a.provider === "ollama"
            label: i18n("Ollama address")
            text: aiCard.a.ollamaUrl
            onEditingFinished: if (text.trim() && text !== aiCard.a.ollamaUrl) {
                AppController.setSetting("ollama_url", text.trim())
                AppController.refreshOllamaModels()
            }
        }
        FormCard.FormComboBoxDelegate {
            visible: aiCard.a.provider === "ollama"
            text: i18n("Model")
            description: AppController.ollamaModels.length === 0 ? i18n("No models found yet; is Ollama running?") : ""
            model: AppController.ollamaModels
            currentIndex: AppController.ollamaModels.indexOf(aiCard.a.ollamaModel)
            onActivated: AppController.setSetting("ollama_model", currentText)
            Component.onCompleted: if (aiCard.a.provider === "ollama") AppController.refreshOllamaModels()
        }

        FormCard.FormDelegateSeparator { visible: aiCard.a.provider !== "off" }
        FormCard.FormButtonDelegate {
            visible: aiCard.a.provider !== "off"
            enabled: aiCard.a.ready && !aiCard.a.busy
            icon.name: "network-connect"
            text: aiCard.a.busy ? i18n("Checking…") : i18n("Test connection")
            onClicked: AppController.testAi()
        }
    }

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

    FormCard.FormHeader { title: i18n("While working") }
    FormCard.FormCard {
        FormCard.FormSpinBoxDelegate {
            label: i18n("Ask about time away after (minutes idle, 0 to never ask)")
            from: 0
            to: 120
            stepSize: 5
            value: page.s.idleMinutes
            onValueModified: AppController.setSetting("idle_minutes", value)
        }
    }

    FormCard.FormHeader { title: i18n("Other apps") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Let other apps change tasks")
            description: i18n("Scripts, KRunner and AI agents (through MCP) can add, finish and schedule tasks. Reading is always allowed. Changes can be undone here.")
            checked: page.s.externalWrites
            onToggled: AppController.setSetting("external_writes", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSwitchDelegate {
            text: i18n("Show in KRunner")
            description: i18n("Alt+Space, then “tt call mum tomorrow” to capture, or type a task's name to start or finish it")
            checked: page.s.krunner
            onToggled: AppController.setSetting("krunner", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("AI agents (MCP)")
            description: i18n("Add a stdio MCP server that runs: task-tumbler --mcp. For Claude Code: claude mcp add task-tumbler -- task-tumbler --mcp")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("From a terminal")
            description: i18n("task-tumbler --add \"call mum tomorrow at 6pm\"")
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
        FormCard.FormDelegateSeparator {}
        FormCard.FormSpinBoxDelegate {
            label: i18n("Break after each planned block (minutes)")
            from: 0
            to: 60
            stepSize: 5
            value: page.s.breakMinutes
            onValueModified: AppController.setSetting("break_duration_minutes", value)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextAreaDelegate {
            label: i18n("Notes for the AI planner")
            description: i18n("Anything that helps it plan for you, e.g. \"admin goes after lunch\"")
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
