import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.prison as Prison
import org.kde.tasktumbler

FormCard.FormCardPage {
    id: page

    readonly property var s: AppController.settings
    readonly property bool mac: Qt.platform.os === "osx"
    readonly property bool deviceCalendars: SyncHub.deviceCalendars().supported
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
            text: i18n("Include task details")
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

    FormCard.FormHeader { title: i18n("Linear") }
    FormCard.FormCard {
        id: linearCard
        readonly property var l: SyncHub.state.linear || ({})
        readonly property var teamKeys: linearCard.l.teamKeys || []

        FormCard.FormTextDelegate {
            visible: !linearCard.l.enabled
            text: i18n("Issues assigned to you, as tasks")
            description: i18n("Finishing one here marks it done in Linear. Make a personal API key in Linear: Settings → Security & access → Personal API keys.")
        }
        FormCard.FormPasswordFieldDelegate {
            id: linearKey
            visible: !linearCard.l.enabled
            label: i18n("Personal API key")
            placeholderText: "lin_api_…"
            onAccepted: if (text.trim()) { SyncHub.connectLinear(text); text = "" }
        }
        FormCard.FormButtonDelegate {
            visible: !linearCard.l.enabled
            enabled: linearKey.text.trim() !== ""
            icon.name: "network-connect"
            text: i18n("Connect")
            onClicked: { SyncHub.connectLinear(linearKey.text); linearKey.text = "" }
        }

        FormCard.FormTextDelegate {
            visible: !!linearCard.l.enabled
            text: i18n("Connected as %1", linearCard.l.user || "?")
            description: linearCard.l.error
                         || (linearCard.l.lastSync ? i18n("%1 · synced at %2", linearCard.l.summary, Qt.formatTime(new Date(linearCard.l.lastSync), Qt.locale().timeFormat(Locale.ShortFormat)))
                                                   : (SyncHub.state.syncing ? i18n("Syncing…") : i18n("Not synced yet")))
            descriptionItem.color: linearCard.l.error ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
        }
        Repeater {
            model: linearCard.l.enabled ? (linearCard.l.teams || []) : []
            FormCard.FormCheckDelegate {
                required property var modelData
                text: i18n("%1 (%2)", modelData.name, modelData.key)
                description: linearCard.teamKeys.length === 0 ? i18n("All teams are synced until you pick some") : ""
                checked: linearCard.teamKeys.length === 0 || linearCard.teamKeys.indexOf(modelData.key) >= 0
                onToggled: {
                    let keys = linearCard.teamKeys.length === 0 ? (linearCard.l.teams || []).map(t => t.key) : linearCard.teamKeys.slice()
                    if (checked && keys.indexOf(modelData.key) < 0) keys.push(modelData.key)
                    if (!checked) keys = keys.filter(k => k !== modelData.key)
                    SyncHub.setLinearTeams(keys)
                }
            }
        }
        FormCard.FormSwitchDelegate {
            visible: !!linearCard.l.enabled
            text: i18n("Include backlog")
            description: i18n("Off: only Todo and In Progress issues")
            checked: !!linearCard.l.includeBacklog
            onToggled: SyncHub.setLinearBacklog(checked)
        }
        FormCard.FormButtonDelegate {
            visible: !!linearCard.l.enabled
            enabled: !SyncHub.state.syncing
            icon.name: "view-refresh"
            text: SyncHub.state.syncing ? i18n("Syncing…") : i18n("Sync now")
            onClicked: SyncHub.syncNow()
        }
        FormCard.FormButtonDelegate {
            visible: !!linearCard.l.enabled
            icon.name: "network-disconnect"
            text: i18n("Disconnect")
            description: i18n("Tasks already brought in stay")
            onClicked: SyncHub.disconnectLinear()
        }
    }

    FormCard.FormHeader { title: i18n("Calendars") }
    FormCard.FormCard {
        id: feedsCard
        Repeater {
            model: SyncHub.state.feeds || []
            FormCard.FormSwitchDelegate {
                required property var modelData
                text: modelData.name
                description: (modelData.error ? "⚠ " + modelData.error : "")
                             || (modelData.lastSync ? i18np("1 event in the next two months", "%1 events in the next two months", modelData.count)
                                                    : (modelData.enabled ? i18n("Not synced yet") : i18n("Off")))
                checked: modelData.enabled
                onToggled: SyncHub.updateFeed(modelData.id, { enabled: checked })
                leading: Rectangle {
                    implicitWidth: Kirigami.Units.gridUnit * 0.8
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: modelData.color
                }
                trailing: Controls.ToolButton {
                    icon.name: "list-remove"
                    text: i18n("Unsubscribe")
                    display: Controls.AbstractButton.IconOnly
                    onClicked: SyncHub.removeFeed(modelData.id)
                    Controls.ToolTip.text: text
                    Controls.ToolTip.visible: hovered
                    Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
            }
        }
        FormCard.FormButtonDelegate {
            visible: page.deviceCalendars
            icon.name: "view-calendar"
            text: i18n("Calendars on this Mac…")
            description: (SyncHub.state.feeds || []).length === 0 ? i18n("iCloud, Google, Exchange: whatever's in the Calendar app, updated as it changes") : ""
            onClicked: deviceCalendarsDialog.start()
        }
        FormCard.FormButtonDelegate {
            icon.name: "list-add"
            text: page.deviceCalendars ? i18n("Subscribe by address…") : i18n("Subscribe to a calendar…")
            description: (SyncHub.state.feeds || []).length === 0 ? i18n("Its events show on your schedule and keep tasks out of busy times. Read-only; refreshed every 15 minutes.") : ""
            onClicked: feedDialog.start()
        }
        FormCard.FormButtonDelegate {
            visible: (SyncHub.state.feeds || []).length > 0
            enabled: !SyncHub.state.syncing
            icon.name: "view-refresh"
            text: SyncHub.state.syncing ? i18n("Syncing…") : i18n("Refresh now")
            onClicked: SyncHub.syncNow()
        }
    }
    FeedDialog { id: feedDialog }
    DeviceCalendarsDialog { id: deviceCalendarsDialog }

    FormCard.FormHeader { title: i18n("Phone") }
    FormCard.FormCard {
        id: phoneCard
        readonly property var r: RemoteHub.state

        FormCard.FormSwitchDelegate {
            text: i18n("Phone companion")
            description: i18n("Today, quick capture and reminders on your phone, over your tailnet")
            checked: phoneCard.r.enabled
            onToggled: RemoteHub.setEnabled(checked)
        }
        FormCard.FormTextDelegate {
            visible: phoneCard.r.enabled
            text: phoneCard.r.running ? i18n("Listening on %1", phoneCard.r.listen) : i18n("Not running")
            description: phoneCard.r.error || (phoneCard.r.streams > 0 ? i18np("1 phone connected", "%1 phones connected", phoneCard.r.streams) : "")
            textItem.color: phoneCard.r.error ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
        }

        FormCard.FormDelegateSeparator { visible: phoneCard.r.enabled }
        FormCard.FormTextFieldDelegate {
            visible: phoneCard.r.enabled
            label: i18n("Address on your tailnet")
            placeholderText: "https://this-machine.tailnet.ts.net:7789"
            text: phoneCard.r.publicUrl
            onEditingFinished: if (text !== phoneCard.r.publicUrl) RemoteHub.setPublicUrl(text)
            trailing: Controls.Button {
                text: i18n("Detect")
                icon.name: "network-vpn"
                onClicked: RemoteHub.detectTailscale()
            }
        }
        FormCard.FormTextDelegate {
            visible: phoneCard.r.enabled
            text: i18n("Run this once to put it on your tailnet with HTTPS")
            description: phoneCard.r.serveCommand
            descriptionItem.font.family: "monospace"
            trailing: Controls.Button {
                text: i18n("Copy")
                icon.name: "edit-copy"
                onClicked: RemoteHub.copyToClipboard(phoneCard.r.serveCommand)
            }
        }

        FormCard.FormDelegateSeparator { visible: phoneCard.r.enabled && phoneCard.r.running }
        FormCard.AbstractFormDelegate {
            visible: phoneCard.r.enabled && phoneCard.r.running
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Rectangle {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 10
                    color: "white"
                    radius: Kirigami.Units.cornerRadius
                    Prison.Barcode {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing
                        barcodeType: Prison.Barcode.QRCode
                        content: phoneCard.r.pairUrl
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.weight: Font.DemiBold
                        text: i18n("Scan with your phone to pair")
                    }
                    Controls.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Kirigami.Theme.disabledTextColor
                        text: phoneCard.r.secure
                              ? i18n("Then add it to the home screen and turn on notifications from its menu.")
                              : i18n("Set the tailnet https:// address above first: phones only allow notifications and offline use over HTTPS.")
                    }
                    Controls.Button {
                        text: i18n("New pairing code")
                        icon.name: "view-refresh"
                        onClicked: RemoteHub.rotateToken()
                        Controls.ToolTip.text: i18n("Phones paired with the old code stop working and have to scan again")
                        Controls.ToolTip.visible: hovered
                        Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }
            }
        }

        Repeater {
            model: phoneCard.r.enabled ? phoneCard.r.phones : []
            FormCard.FormTextDelegate {
                required property var modelData
                text: modelData.label
                description: i18n("Gets notifications")
                trailing: Controls.ToolButton {
                    icon.name: "list-remove"
                    text: i18n("Forget")
                    display: Controls.AbstractButton.IconOnly
                    onClicked: RemoteHub.forgetPhone(modelData.endpoint)
                }
            }
        }
        FormCard.FormComboBoxDelegate {
            visible: phoneCard.r.enabled
            text: i18n("Send reminders to the phone")
            model: [
                { value: "away", text: i18n("When I'm away from the computer") },
                { value: "both", text: i18n("Always, as well as the desktop") },
                { value: "desktop", text: i18n("Never") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: Math.max(0, model.findIndex(m => m.value === phoneCard.r.notify))
            onActivated: AppController.setSetting("remote_notify", currentValue)
        }
        FormCard.FormButtonDelegate {
            visible: phoneCard.r.enabled && phoneCard.r.phones.length > 0
            icon.name: "notifications"
            text: i18n("Send a test notification")
            onClicked: RemoteHub.sendTest()
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
            text: page.mac ? i18n("Keep running in the menu bar when closed") : i18n("Keep running in the system tray when closed")
            description: i18n("Reminders only work while Task Tumbler is running")
            checked: page.s.closeToTray
            onToggled: AppController.setSetting("close_to_tray", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSwitchDelegate {
            text: i18n("Start at login")
            description: page.mac ? i18n("Starts quietly in the menu bar") : i18n("Starts quietly in the tray")
            checked: page.s.autostart
            onToggled: AppController.setSetting("autostart", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("Quick capture from anywhere")
            description: page.mac ? i18n("⇧⌘Space")
                                  : i18n("Meta+Shift+Space by default; change it in System Settings → Shortcuts → Task Tumbler")
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
            description: page.mac ? i18n("AI agents (through MCP) can add, finish and schedule tasks. Reading is always allowed. Changes can be undone here.")
                                  : i18n("Scripts, KRunner and AI agents (through MCP) can add, finish and schedule tasks. Reading is always allowed. Changes can be undone here.")
            checked: page.s.externalWrites
            onToggled: AppController.setSetting("external_writes", checked)
        }
        FormCard.FormDelegateSeparator { visible: !page.mac }
        FormCard.FormSwitchDelegate {
            visible: !page.mac // KRunner is Plasma's
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

    FormCard.FormHeader { title: i18n("Energy and planning") }
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
        FormCard.FormSpinBoxDelegate {
            label: i18n("Break after each planned block (minutes)")
            from: 0
            to: 60
            stepSize: 5
            value: page.s.breakMinutes
            onValueModified: AppController.setSetting("break_duration_minutes", value)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSwitchDelegate {
            text: i18n("Plan with your real pace")
            description: i18n("Once you've finished a few timed tasks, new blocks allow for how long things like them usually take you, up to double the estimate. They're never made shorter.")
            checked: page.s.planWithPace
            onToggled: AppController.setSetting("plan_with_pace", checked)
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextAreaDelegate {
            label: i18n("Notes for the AI planner")
            description: i18n("Anything that helps it plan for you, e.g. \"admin goes after lunch\"")
            text: page.s.aiHints
            onEditingFinished: if (text !== page.s.aiHints) AppController.setSetting("ai_scheduling_hints", text)
        }
    }

    FormCard.FormHeader { title: i18n("Winding down") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            id: closeOutSwitch
            text: i18n("Nudge me to close out the day")
            description: i18n("A note on Today (and a notification) once the evening comes, until you've closed out.")
            checked: page.s.closeOutTime !== ""
            onToggled: AppController.setSetting("closeout_time", checked ? "17:30" : "")
        }
        FormCard.FormDelegateSeparator { visible: closeOutSwitch.checked }
        FormCard.FormTextFieldDelegate {
            visible: closeOutSwitch.checked
            label: i18n("From (HH:MM)")
            text: page.s.closeOutTime
            validator: RegularExpressionValidator { regularExpression: /^([01]?\d|2[0-3]):[0-5]\d$/ }
            // Stored zero-padded: the controller reads it as HH:mm.
            onEditingFinished: {
                if (!acceptableInput)
                    return
                const value = text.length === 4 ? "0" + text : text
                if (value !== page.s.closeOutTime)
                    AppController.setSetting("closeout_time", value)
            }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            text: i18n("Weekly review")
            description: i18n("Shown on Today from this day until you've done it.")
            textRole: "text"
            valueRole: "value"
            model: [
                { text: i18n("Off"), value: -1 },
                { text: Qt.locale().dayName(0), value: 0 },
                { text: Qt.locale().dayName(1), value: 1 },
                { text: Qt.locale().dayName(2), value: 2 },
                { text: Qt.locale().dayName(3), value: 3 },
                { text: Qt.locale().dayName(4), value: 4 },
                { text: Qt.locale().dayName(5), value: 5 },
                { text: Qt.locale().dayName(6), value: 6 }
            ]
            Component.onCompleted: currentIndex = indexOfValue(page.s.reviewWeekday)
            onActivated: AppController.setSetting("review_weekday", currentValue)
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
