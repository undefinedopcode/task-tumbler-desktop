import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// A small window for getting a thought out of your head in one line. Works
// even while the main window is hidden in the tray (global shortcut).
Window {
    id: win

    property var preview: ({})
    property bool asking: false

    function openCapture() {
        field.text = ""
        preview = {}
        asking = false
        show()
        raise()
        requestActivate()
        field.forceActiveFocus()
    }

    function submit() {
        if (AppController.capture(field.text) !== "")
            win.close()
    }

    title: i18n("Quick Capture")
    flags: Qt.Dialog | Qt.WindowStaysOnTopHint
    modality: Qt.NonModal
    width: Kirigami.Units.gridUnit * 34
    height: content.implicitHeight + Kirigami.Units.largeSpacing * 3
    minimumWidth: Kirigami.Units.gridUnit * 24
    color: Kirigami.Theme.backgroundColor

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    onActiveChanged: if (!active && visible && field.text === "") win.close()

    Connections {
        target: AppController
        function onAiCaptureReady(text, preview) {
            win.asking = false
            if (text === field.text)
                win.preview = preview
        }
        function onAiFailed(feature, error) {
            if (feature === "capture") {
                win.asking = false
                win.preview = Object.assign({}, win.preview, { aiError: error })
            }
        }
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing * 1.5
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Kirigami.Icon {
                source: "qrc:/icons/org.kde.tasktumbler.svg"
                fallback: "view-task-add"
                implicitWidth: Kirigami.Units.iconSizes.medium
                implicitHeight: implicitWidth
            }
            Kirigami.Heading {
                level: 3
                text: i18n("What's on your mind?")
            }
        }

        Controls.TextField {
            id: field
            Layout.fillWidth: true
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.2
            placeholderText: i18n("call dentist tomorrow at 3pm ~15m #phone")
            onTextChanged: win.preview = text.trim() ? AppController.previewCapture(text) : ({})
            onAccepted: win.submit()
            Keys.onEscapePressed: win.close()
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)
                        && AppController.ai.ready && text.trim() !== "") {
                    win.asking = true
                    AppController.aiCapture(text)
                    event.accepted = true
                }
            }
        }

        // What will be created, live, so nothing is surprising.
        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: win.preview.title !== undefined

            Chip {
                visible: !!win.preview.fromAi
                text: i18n("Read by AI")
                iconName: "tools-wizard"
                tint: Kirigami.Theme.positiveTextColor
                strong: true
            }
            Chip {
                text: win.preview.title || i18n("(no title yet)")
                tint: Kirigami.Theme.highlightColor
                strong: true
            }
            Chip { visible: !!win.preview.date; text: win.preview.date || ""; iconName: "view-calendar" }
            Chip { visible: !!win.preview.time; text: win.preview.time || ""; iconName: "chronometer" }
            Chip { visible: !!win.preview.recurrence; text: win.preview.recurrence || ""; iconName: "view-refresh" }
            Chip { visible: !!win.preview.estimate; text: win.preview.estimate ? Format.minutes(win.preview.estimate) : "" }
            Chip {
                visible: !!win.preview.priority
                text: win.preview.priority ? Format.priorityLabel(win.preview.priority) : ""
                tint: win.preview.priority ? Format.priorityColors[win.preview.priority] : "transparent"
                strong: true
            }
            Chip { visible: !!win.preview.energy; text: win.preview.energy ? Format.energyLabel(win.preview.energy) : "" }
            Chip {
                visible: !!win.preview.category
                text: win.preview.category || ""
                iconName: win.preview.categoryKnown ? "tag" : "dialog-warning"
                tint: win.preview.categoryKnown ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.neutralTextColor
                strong: !win.preview.categoryKnown
            }
            Repeater {
                model: win.preview.tags || []
                Chip {
                    required property string modelData
                    text: "#" + modelData
                }
            }
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            text: i18n("Optional: tomorrow · friday · at 3pm · ~30m · !high · +low (energy) · #tag · /category · every mon and thu · daily")
        }

        Controls.Label {
            visible: !!win.preview.aiError
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.negativeTextColor
            text: i18n("AI couldn't read it: %1", win.preview.aiError || "")
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                visible: AppController.ai.ready
                enabled: field.text.trim() !== "" && !win.asking && !win.preview.fromAi
                text: win.asking ? i18n("Asking…") : i18n("Ask AI")
                icon.name: "tools-wizard"
                onClicked: {
                    win.asking = true
                    AppController.aiCapture(field.text)
                }
                Controls.ToolTip.text: i18n("Let the AI read a messy note (Ctrl+Enter)")
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            Item { Layout.fillWidth: true }
            Controls.Button {
                text: i18n("Cancel")
                onClicked: win.close()
            }
            Controls.Button {
                text: i18n("Add")
                icon.name: "list-add"
                highlighted: true
                enabled: field.text.trim() !== ""
                onClicked: win.submit()
            }
        }
    }
}
